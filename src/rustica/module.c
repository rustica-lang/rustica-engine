/*
 * Copyright (c) 2024-present 燕几（北京）科技有限公司
 *
 * Rustica (runtime) is licensed under Mulan PSL v2. You can use this
 * software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *              http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
 * OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the Mulan PSL v2 for more details.
 */

#include "postgres.h"
#include "executor/spi.h"
#include "tcop/tcopprot.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

#include "rustica/module.h"
#include "rustica/utils.h"

static SPIPlanPtr load_module_plan = NULL;
static SPIPlanPtr load_module_queries_plan = NULL;
static const char *load_module_sql =
    "SELECT bin_code, heap_types FROM rustica.modules WHERE name = $1";
static const char *load_module_queries_sql =
    "SELECT * FROM rustica.queries WHERE module = $1 ORDER BY index";

static PreparedModule *
create_module_with_queries(Datum name);

static void
load_heap_types(ArrayType *array, CommonHeapTypes *heap_types);

static AOTModule *
load_aot_module(const char *name, uint8 *bin_code, uint32_t bin_code_len);

void
rst_module_worker_startup() {
    debug_query_string = load_module_sql;
    load_module_plan = SPI_prepare(load_module_sql, 1, (Oid[1]){ TEXTOID });
    if (!load_module_plan)
        ereport(ERROR,
                errmsg("could not prepare SPI plan: %s",
                       SPI_result_code_string(SPI_result)));
    if (SPI_keepplan(load_module_plan))
        ereport(ERROR, errmsg("failed to keep plan"));

    debug_query_string = load_module_queries_sql;
    load_module_queries_plan =
        SPI_prepare(load_module_queries_sql, 1, (Oid[1]){ TEXTOID });
    if (!load_module_queries_plan)
        ereport(ERROR,
                (errmsg("could not prepare SPI plan: %s",
                        SPI_result_code_string(SPI_result))));
    if (SPI_keepplan(load_module_queries_plan))
        ereport(ERROR, (errmsg("failed to keep plan")));
    debug_query_string = NULL;
}

void
rst_module_worker_teardown() {
    SPI_freeplan(load_module_plan);
    SPI_freeplan(load_module_queries_plan);
}

PreparedModule *
rst_prepare_module(const char *name, uint8 **buffer, uint32 *size) {
    // We use wasm_module_t->name as a pointer to PreparedModule which starts
    // with maximum-128 chars of name, so we had to limit the name length here.
    if (strlen(name) > RST_MODULE_NAME_MAXLEN)
        ereport(ERROR,
                errmsg("module name too long (%ld bytes): maximum %d bytes",
                       strlen(name),
                       RST_MODULE_NAME_MAXLEN));

    PreparedModule *pmod = NULL;
    SPITupleTable *tuptable = NULL;
    Datum name_datum = CStringGetTextDatum(name);

    PG_TRY();
    {
        PG_TRY(2);
        {
            // Create the PreparedModule with all pre-compiled queries
            pmod = create_module_with_queries(name_datum);

            // Query the rustica.modules table
            debug_query_string = load_module_sql;
            int ret =
                SPI_execute_plan(load_module_plan, &name_datum, NULL, true, 1);
            if (ret != SPI_OK_SELECT)
                ereport(ERROR,
                        errmsg("failed to load module \"%s\": %s",
                               name,
                               SPI_result_code_string(ret)));
            tuptable = SPI_tuptable;
            if (SPI_processed == 0)
                ereport(ERROR,
                        errcode(ERRCODE_NO_DATA_FOUND),
                        errmsg("module \"%s\" doesn't exist", name));

            // Take out the raw data from the tuptable
            bool isnull;
            Datum datum =
                SPI_getbinval(tuptable->vals[0], tuptable->tupdesc, 1, &isnull);
            Assert(!isnull);
            bytea *bin_code = DatumGetByteaPP(datum);
            datum =
                SPI_getbinval(tuptable->vals[0], tuptable->tupdesc, 2, &isnull);
            Assert(!isnull);
            ArrayType *heap_types = DatumGetArrayTypeP(datum);
            debug_query_string = NULL;

            // Load heap_types and the actual WASM module
            load_heap_types(heap_types, &pmod->heap_types);
            if (buffer) {
                Assert(size != NULL);
                *buffer = (uint8 *)VARDATA_ANY(bin_code);
                *size = VARSIZE_ANY_EXHDR(bin_code);
                pmod->loading_tuptable = tuptable;
            }
            else {
                pmod->module = load_aot_module((const char *)pmod,
                                               (uint8 *)VARDATA_ANY(bin_code),
                                               VARSIZE_ANY_EXHDR(bin_code));
                SPI_freetuptable(tuptable);
                tuptable = NULL;
            }
        }
        PG_CATCH(2);
        {
            rst_free_module(pmod);
            SPI_freetuptable(tuptable);
            PG_RE_THROW();
        }
        PG_END_TRY(2);
    }
    PG_FINALLY();
    {
        pfree(DatumGetPointer(name_datum));
        debug_query_string = NULL;
    }
    PG_END_TRY();

    return pmod;
}

PreparedModule *
rst_lookup_module(const char *name) {
    wasm_module_t module = wasm_runtime_find_module_registered(name);
    if (module)
        return (PreparedModule *)wasm_runtime_get_module_name(module);
    else
        return NULL;
}

void
rst_free_module(PreparedModule *pmod) {
    if (!pmod)
        return;
    for (int i = 0; i < pmod->nqueries; i++)
        rst_free_query_plan(&pmod->queries[i]);
    if (pmod->module) {
        wasm_runtime_unregister_module((wasm_module_t)pmod->module);
        aot_unload(pmod->module);
    }
    if (pmod->loading_tuptable)
        SPI_freetuptable(pmod->loading_tuptable);
    pfree(pmod);
}

wasm_exec_env_t
rst_module_instantiate(PreparedModule *pmod,
                       uint32 stack_size,
                       uint32 heap_size) {
    DECLARE_ERROR_BUF(128);

    // Instantiate the WASM module
    wasm_module_inst_t instance =
        wasm_runtime_instantiate((wasm_module_t)pmod->module,
                                 stack_size,
                                 heap_size,
                                 ERROR_BUF_PARAMS);
    if (!instance)
        ereport(ERROR,
                errmsg("failed to instantiate module \"%s\": %s",
                       pmod->name,
                       ERROR_BUF));

    // Create WASM execution environment
    wasm_exec_env_t exec_env =
        wasm_runtime_create_exec_env(instance, stack_size);
    if (!exec_env) {
        wasm_runtime_deinstantiate(instance);
        ereport(
            ERROR,
            errmsg(
                "failed to instantiate module \"%s\": create exec env failed",
                pmod->name));
    }

    return exec_env;
}

static PreparedModule *
create_module_with_queries(Datum name) {
    // Load pre-compiled queries
    debug_query_string = load_module_queries_sql;
    int ret = SPI_execute_plan(load_module_queries_plan, &name, NULL, true, 0);
    if (ret != SPI_OK_SELECT)
        ereport(ERROR,
                errmsg("failed to load module queries: %s",
                       SPI_result_code_string(ret)));
    SPITupleTable *tuptable = SPI_tuptable;
    Assert(tuptable->tupdesc->natts == 12);
    debug_query_string = NULL;

    // Construct the PreparedModule in TopMemoryContext and initialize name,
    // nqueries and all query plans in it.
    PreparedModule *pmod;
    PG_TRY();
    {
        pmod = (PreparedModule *)MemoryContextAllocZero(
            TopMemoryContext,
            sizeof(PreparedModule) + sizeof(QueryPlan) * tuptable->numvals);
        PG_TRY(2);
        {
            memcpy(pmod->name, VARDATA(name), VARSIZE_ANY_EXHDR(name));
            pmod->nqueries = (int)tuptable->numvals;
            bool isnull;
            for (int i = 0; i < pmod->nqueries; i++) {
                int32 idx = DatumGetInt32(SPI_getbinval(tuptable->vals[i],
                                                        tuptable->tupdesc,
                                                        2,
                                                        &isnull));
                Assert(!isnull);
                if (i != idx)
                    ereport(ERROR,
                            errmsg("bad query index %d: expect %d", idx, i));
                rst_init_query_plan(&pmod->queries[i],
                                    tuptable->vals[i],
                                    tuptable->tupdesc);
            }
        }
        PG_CATCH(2);
        {
            rst_free_module(pmod);
            PG_RE_THROW();
        }
        PG_END_TRY(2);
    }
    PG_FINALLY();
    {
        SPI_freetuptable(tuptable);
    }
    PG_END_TRY();

    return pmod;
}

static void
load_heap_types(ArrayType *array, CommonHeapTypes *heap_types) {
    int num;
    Datum *datums;
    deconstruct_array(array,
                      INT4OID,
                      sizeof(int32_t),
                      true,
                      'i',
                      &datums,
                      NULL,
                      &num);
    int expected_num = sizeof(CommonHeapTypes) / sizeof(int32_t);
    if (expected_num != num)
        ereport(
            ERROR,
            errmsg("expected %d heap types, actual: %d", expected_num, num));
    int32_t *ptr = (int32_t *)heap_types;
    for (int i = 0; i < num; i++) {
        ptr[i] = DatumGetInt32(datums[i]);
    }
    pfree(datums);
}

static AOTModule *
load_aot_module(const char *name, uint8 *bin_code, uint32_t bin_code_len) {
    DECLARE_ERROR_BUF(128);

    // Load the WASM module
    LoadArgs load_args = { .name = (char *)name, .wasm_binary_freeable = true };
    AOTModule *aot_module = NULL;
    MemoryContext tx_mctx = MemoryContextSwitchTo(TopMemoryContext);
    PG_TRY();
    {
        wasm_module_t module = wasm_runtime_load_ex(bin_code,
                                                    bin_code_len,
                                                    &load_args,
                                                    ERROR_BUF_PARAMS);
        if (!module)
            ereport(ERROR,
                    errmsg("bad WASM bin_code of module \"%s\": %s",
                           name,
                           ERROR_BUF));
        if (module->module_type == Wasm_Module_Bytecode) {
            wasm_unload((WASMModule *)module);
            ereport(ERROR,
                    errmsg("WASM bin_code of module \"%s\" is not AoT-compiled",
                           name));
        }
        Assert(module->module_type == Wasm_Module_AoT);
        aot_module = (AOTModule *)module;

        PG_TRY(2);
        {
            // Due to the lazy loading of `rtt_types` objects which are
            // allocated in the module's instantiate phase and released with
            // popping transactional memory context, potential crashes can
            // occur. To resolve this issue, we preload all rtt_type objects
            // within the TopMemoryContext.
            for (uint32 i = 0; i < aot_module->type_count; i++) {
                if (!wasm_rtt_type_new(aot_module->types[i],
                                       i,
                                       aot_module->rtt_types,
                                       aot_module->type_count,
                                       &aot_module->rtt_type_lock))
                    ereport(ERROR, errmsg("failed to create rtt object"));
            }
            if (!wasm_runtime_register_module(name, module, ERROR_BUF_PARAMS))
                ereport(ERROR,
                        errmsg("cannot register module \"%s\": %s",
                               name,
                               ERROR_BUF));
        }
        PG_CATCH(2);
        {
            aot_unload(aot_module);
            PG_RE_THROW();
        }
        PG_END_TRY(2);
    }
    PG_FINALLY();
    {
        MemoryContextSwitchTo(tx_mctx);
    }
    PG_END_TRY();

    return aot_module;
}
