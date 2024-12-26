/*
 * Copyright (c) 2024 燕几（北京）科技有限公司
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

#include "aot_runtime.h"

#include "rustica/module.h"
#include "rustica/utils.h"

static SPIPlanPtr load_module_plan = NULL;
static const char *load_module_sql =
    "SELECT bin_code FROM rustica.modules WHERE name = $1";

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
    debug_query_string = NULL;
}

void
rst_module_worker_teardown() {
    SPI_freeplan(load_module_plan);
}

wasm_module_t
rst_load_module(const char *name, uint8 **buffer, uint32 *size) {
    wasm_module_t module = NULL;
    Datum name_datum = CStringGetTextDatum(name);

    PG_TRY();
    {
        debug_query_string = load_module_sql;
        int ret =
            SPI_execute_plan(load_module_plan, &name_datum, NULL, true, 1);
        if (ret != SPI_OK_SELECT)
            ereport(ERROR,
                    errmsg("failed to load module \"%s\": %s",
                           name,
                           SPI_result_code_string(ret)));
        if (SPI_processed == 0)
            ereport(ERROR,
                    errcode(ERRCODE_NO_DATA_FOUND),
                    errmsg("module \"%s\" doesn't exist", name));

        bool isnull;
        bytea *bin_code = DatumGetByteaPP(SPI_getbinval(SPI_tuptable->vals[0],
                                                        SPI_tuptable->tupdesc,
                                                        1,
                                                        &isnull));
        Assert(!isnull);

        if (buffer) {
            Assert(size != NULL);
            *buffer = (uint8 *)VARDATA_ANY(bin_code);
            *size = VARSIZE_ANY_EXHDR(bin_code);
            Assert(*buffer != NULL);
        }
        else {
            module = load_aot_module(name,
                                     (uint8 *)VARDATA_ANY(bin_code),
                                     VARSIZE_ANY_EXHDR(bin_code));
        }
    }
    PG_FINALLY();
    {
        pfree(DatumGetPointer(name_datum));
        debug_query_string = NULL;
    }
    PG_END_TRY();

    return module;
}

wasm_module_t
rst_lookup_module(const char *name) {
    return wasm_runtime_find_module_registered(name);
}

void
rst_free_module(wasm_module_t module) {
    if (!module)
        return;

    wasm_runtime_unregister_module(module);
    // since `wasm_runtime_unload` function does not call `aot_unload` when
    // multi-modules is enabled, we have to call `aot_unload` directly here
    aot_unload((AOTModule *)module);
}

wasm_exec_env_t
rst_module_instantiate(wasm_module_t module,
                       uint32 stack_size,
                       uint32 heap_size) {
    DECLARE_ERROR_BUF(128);

    // Instantiate the WASM module
    wasm_module_inst_t instance = wasm_runtime_instantiate(module,
                                                           stack_size,
                                                           heap_size,
                                                           ERROR_BUF_PARAMS);
    if (!instance)
        ereport(ERROR, errmsg("failed to instantiate: %s", ERROR_BUF));

    // Create WASM execution environment
    wasm_exec_env_t exec_env =
        wasm_runtime_create_exec_env(instance, stack_size);
    if (!exec_env) {
        wasm_runtime_deinstantiate(instance);
        ereport(ERROR, errmsg("failed to instantiate: create exec env failed"));
    }

    return exec_env;
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
            // due to the lazy loading of `rtt_types` objects which are
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
                    ereport(ERROR, errmsg("create rtt object failed"));
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
    { MemoryContextSwitchTo(tx_mctx); }
    PG_END_TRY();

    return aot_module;
}
