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
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "tcop/utility.h"

#include "wasm_runtime_common.h"
#include "aot_runtime.h"

#include "rustica/query.h"

AppPlan *app_plans = NULL;
static int app_plans_size = 0;
static size_t app_plans_allocated = 2;

static SPITupleTable **tuptables = NULL;
int tuptables_size = 0;
static size_t tuptables_allocated = 2;

wasm_struct_type_t
get_query_struct_type_by_sql_idx(wasm_exec_env_t exec_env, int32_t idx) {
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    wasm_function_inst_t func_inst = ctx->get_queries;
    wasm_module_inst_t module_inst = wasm_exec_env_get_module_inst(exec_env);
    WASMModuleCommon *module = wasm_exec_env_get_module(exec_env);

    wasm_func_type_t func_type =
        wasm_runtime_get_function_type(func_inst, module_inst->module_type);

    // Queries
    WASMStructType *queries_struct_type =
        (WASMStructType *)wasm_get_defined_type(
            module,
            wasm_func_type_get_result_type(func_type, 0).heap_type);
    Assert(queries_struct_type->base_type.type_flag == WASM_TYPE_STRUCT);

    if (idx >= wasm_struct_type_get_field_count(
            (WASMStructType *)queries_struct_type)) {
        ereport(WARNING,
                (errmsg("failed to get query struct type for sql #%d", idx)));
        return NULL;
    }

    // Query[ARGS, RET]
    WASMStructType *query_struct_type = (WASMStructType *)wasm_get_defined_type(
        module,
        wasm_struct_type_get_field_type(queries_struct_type, idx, false)
            .heap_type);
    Assert(queries_struct_type->base_type.type_flag == WASM_TYPE_STRUCT);
    return query_struct_type;
}

wasm_struct_type_t
get_query_field_struct_type(wasm_exec_env_t exec_env,
                            int32_t sql_idx,
                            int32_t field_idx) {
    // Query[ARGS, RET]
    WASMStructType *query_struct_type;
    if (!(query_struct_type =
              get_query_struct_type_by_sql_idx(exec_env, sql_idx))) {
        return NULL;
    }

    WASMModuleCommon *module = wasm_exec_env_get_module(exec_env);
    // ARGS - 0, RET - 1
    WASMStructType *field_struct_type = (WASMStructType *)wasm_get_defined_type(
        module,
        wasm_struct_type_get_field_type(query_struct_type, field_idx, false)
            .heap_type);
    Assert(field_struct_type->base_type.type_flag == WASM_TYPE_STRUCT);
    return field_struct_type;
}

wasm_struct_type_t
get_query_args_struct_type(wasm_exec_env_t exec_env, int32_t sql_idx) {
    return get_query_field_struct_type(exec_env, sql_idx, 0);
}

wasm_struct_type_t
get_query_ret_struct_type(wasm_exec_env_t exec_env, int32_t sql_idx) {
    return get_query_field_struct_type(exec_env, sql_idx, 1);
}

wasm_struct_type_t
get_ret_row_struct_type(wasm_exec_env_t exec_env, int32_t sql_idx) {
    // $TupleTable[T]
    WASMStructType *tuple_table_struct_type;
    if (!(tuple_table_struct_type =
              get_query_ret_struct_type(exec_env, sql_idx))) {
        return NULL;
    }

    WASMModuleCommon *module = wasm_exec_env_get_module(exec_env);

    // $@moonbitlang/core/builtin.Array<T> - struct
    WASMStructType *rows_struct_type = (WASMStructType *)wasm_get_defined_type(
        module,
        wasm_struct_type_get_field_type(tuple_table_struct_type, 1, false)
            .heap_type);
    Assert(rows_struct_type->base_type.type_flag == WASM_TYPE_STRUCT);

    // $FixedArray<UnsafeMaybeUninit<T>>
    WASMArrayType *rows_type = (WASMArrayType *)wasm_get_defined_type(
        module,
        wasm_struct_type_get_field_type(rows_struct_type, 0, false).heap_type);
    Assert(rows_type->base_type.type_flag == WASM_TYPE_ARRAY);

    // T
    WASMStructType *row_struct_type = (WASMStructType *)wasm_get_defined_type(
        module,
        wasm_array_type_get_elem_type(rows_type, false).heap_type);
    Assert(row_struct_type->base_type.type_flag == WASM_TYPE_STRUCT);
    return row_struct_type;
}

bool
is_as_datum_impl(wasm_exec_env_t exec_env, uint32 type_idx) {
    AOTModule *module = (AOTModule *)wasm_exec_env_get_module(exec_env);
    Context *ctx = wasm_runtime_get_user_data(exec_env);

    uint32 *func_type_indexes = module->func_type_indexes;
    for (uint32 i = 0; i < module->func_count; i++) {
        uint32 func_type_idx = func_type_indexes[i];
        AOTFuncType *func_type = (AOTFuncType *)module->types[func_type_idx];
        if (func_type->param_count != 1 || func_type->result_count != 1)
            continue;

        wasm_ref_type_t res_type = wasm_func_type_get_result_type(func_type, 0);
        if (res_type.heap_type != ctx->raw_datum)
            continue;

        wasm_ref_type_t param_type =
            wasm_func_type_get_param_type(func_type, 0);
        if (param_type.value_type == REF_TYPE_ANYREF)
            continue;

        if (param_type.heap_type == type_idx)
            return true;
    }
    return false;
}

static Datum
get_tuple_table_value(int32_t tuptable_idx, int32_t row, int32_t col) {
    if (tuptable_idx < 0 || tuptable_idx > tuptables_size) {
        ereport(ERROR,
                (errmsg("tuple table idx #%d is out of bounds", tuptable_idx)));
    }

    SPITupleTable *tuptable = tuptables[tuptable_idx];
    if (tuptable == NULL) {
        ereport(
            ERROR,
            (errmsg("tuple table at idx #%d is already freed or does not exist",
                    tuptable_idx)));
    }

    if (row < 0 || row >= tuptable->numvals) {
        ereport(ERROR, (errmsg("row idx #%d is out of bounds", row)));
    }

    bool isnull;
    HeapTuple tuple = tuptable->vals[row];
    Datum pg_value = SPI_getbinval(tuple, tuptable->tupdesc, col + 1, &isnull);
    if (pg_value == (Datum)NULL) {
        ereport(ERROR,
                (errmsg("could not get value at row #%d, column #%d",
                        row,
                        col + 1)));
    }

    return pg_value;
}

static RST_WASM_TO_PG_RET
wasm_i32_to_pg_bool(RST_WASM_TO_PG_ARGS) {
    PG_RETURN_BOOL(value.i32 ? true : false);
}

static RST_WASM_TO_PG_RET
wasm_i32_to_pg_int32(RST_WASM_TO_PG_ARGS) {
    PG_RETURN_INT32(value.i32);
}

static RST_WASM_TO_PG_RET
wasm_i64_to_pg_int64(RST_WASM_TO_PG_ARGS) {
    PG_RETURN_INT64(value.i64);
}

static RST_WASM_TO_PG_RET
wasm_bytes_to_pg_text(RST_WASM_TO_PG_ARGS) {
    wasm_array_obj_t arr = (wasm_array_obj_t)value.gc_obj;
    char *buf = (char *)wasm_array_obj_first_elem_addr(arr);
    uint32 len = wasm_array_obj_length(arr);
    PG_RETURN_TEXT_P(cstring_to_text_with_len(buf, len));
}

static RST_WASM_TO_PG_RET
wasm_bytest_to_pg_timestamp(RST_WASM_TO_PG_ARGS) {
    return DirectFunctionCall3(
        timestamp_in,
        CStringGetDatum(wasm_array_obj_first_elem_addr(value.data)),
        ObjectIdGetDatum(InvalidOid),
        Int32GetDatum(-1));
}

static RST_WASM_TO_PG_RET
wasm_as_datum_to_pg_value(RST_WASM_TO_PG_ARGS) {
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);

    wasm_val_t args[1] = { { .kind = WASM_EXTERNREF,
                             .of.foreign = (uintptr_t)value.gc_obj } };
    wasm_val_t results[1];
    wasm_runtime_call_wasm_a(exec_env, ctx->as_raw_datum, 1, results, 1, args);

    WASMStructObjectRef tuple_table =
        (WASMStructObjectRef)results[0].of.foreign;
    wasm_value_t tuple_table_idx;
    wasm_struct_obj_get_field(tuple_table, 0, false, &tuple_table_idx);
    wasm_value_t row;
    wasm_struct_obj_get_field(tuple_table, 1, false, &row);
    wasm_value_t col;
    wasm_struct_obj_get_field(tuple_table, 2, false, &col);

    return get_tuple_table_value(tuple_table_idx.i32, row.i32, col.i32);
}

static RST_PG_TO_WASM_RET
pg_bool_to_wasm_i32(RST_PG_TO_WASM_ARGS) {
    wasm_value_t wasm_value = { .i32 = DatumGetBool(value) };
    return wasm_value;
}

static RST_PG_TO_WASM_RET
pg_int32_to_wasm_i32(RST_PG_TO_WASM_ARGS) {
    wasm_value_t wasm_value = { .i32 = DatumGetInt32(value) };
    return wasm_value;
}

static RST_PG_TO_WASM_RET
pg_int64_to_wasm_i64(RST_PG_TO_WASM_ARGS) {
    wasm_value_t wasm_value = { .i64 = DatumGetInt64(value) };
    return wasm_value;
}

static RST_PG_TO_WASM_RET
pg_text_to_wasm_bytes(RST_PG_TO_WASM_ARGS) {
    wasm_value_t wasm_value;
    text *text_ptr = DatumGetTextPP(value);
    uint32_t size = VARSIZE_ANY_EXHDR(text_ptr) + 1;
    WASMArrayObjectRef array_obj =
        wasm_array_obj_new_with_typeidx(exec_env, type_idx, size, NULL);
    char *data_ptr = wasm_array_obj_first_elem_addr(array_obj);
    text_to_cstring_buffer(text_ptr, data_ptr, size);
    wasm_value.gc_obj = (wasm_obj_t)array_obj;
    return wasm_value;
}

static RST_PG_TO_WASM_RET
pg_timestamp_to_wasm_bytes(RST_PG_TO_WASM_ARGS) {
    wasm_value_t wasm_value;
    char *data = DatumGetCString(DirectFunctionCall1(timestamp_out, value));
    uint32_t size = strlen(data);
    WASMArrayObjectRef array_obj =
        wasm_array_obj_new_with_typeidx(exec_env, type_idx, size, NULL);
    char *data_ptr = wasm_array_obj_first_elem_addr(array_obj);
    memcpy(data_ptr, data, size);
    wasm_value.gc_obj = (wasm_obj_t)array_obj;
    return wasm_value;
}

static RST_PG_TO_WASM_RET
pg_value_to_wasm_as_datum(RST_PG_TO_WASM_ARGS) {
    wasm_value_t wasm_value;
    WASMStructObjectRef datum_obj =
        wasm_struct_obj_new_with_typeidx(exec_env, type_idx);

    wasm_value_t field_val;
    field_val.gc_obj = (wasm_obj_t)tuptable;
    wasm_struct_obj_set_field(datum_obj, 0, &field_val);
    field_val.i32 = (int32)row;
    wasm_struct_obj_set_field(datum_obj, 1, &field_val);
    field_val.i32 = (int32)col;
    wasm_struct_obj_set_field(datum_obj, 2, &field_val);

    wasm_value.gc_obj = (wasm_obj_t)datum_obj;
    return wasm_value;
}

static void
tuple_table_finalizer(const wasm_obj_t obj, void *data) {
    uint32 idx = (uint32)(uintptr_t)data;
    SPI_freetuptable(tuptables[idx]);
    tuptables[idx] = NULL;
}

void
free_app_plans() {
    if (app_plans_size == 0)
        return;

    for (uint32 i = 0; i < app_plans_size; i++) {
        SPI_freeplan(app_plans[i].plan);
        pfree(app_plans[i].wasm_to_pg_funcs);
        pfree(app_plans[i].pg_to_wasm_funcs);
        pfree(app_plans[i].sql);
    }
    app_plans_size = 0;
}

int32_t
env_prepare_statement(wasm_exec_env_t exec_env,
                      WASMArrayObjectRef sql_buf,
                      WASMArrayObjectRef arg_type_oids) {
    MemoryContext tx_mctx = MemoryContextSwitchTo(TopMemoryContext);

    uint32 sql_len = wasm_array_obj_length(sql_buf);
    char *sql = (char *)palloc(sql_len + 1);
    memcpy(sql, sql_buf->elem_data, sql_len);
    if (sql_buf->elem_data[sql_len - 1] != '\0') {
        sql[sql_len] = '\0';
    }

    if (app_plans_size > 0) {
        for (uint32 i = 0; i < app_plans_size; i++) {
            if (strcmp(app_plans[i].sql, sql) == 0) {
                MemoryContextSwitchTo(tx_mctx);
                ereport(
                    DEBUG1,
                    (errmsg("sql #%d is already prepared: \"%s\"", i, sql)));
                return i;
            }
        }
    }

    uint32 sql_idx = app_plans_size++;
    uint32 nargs = wasm_array_obj_length(arg_type_oids) / 4;
    ereport(DEBUG1,
            (errmsg("prepare statement for sql #%d with %d argtypes: \"%s\"",
                    sql_idx,
                    nargs,
                    sql)));

    if (!app_plans) {
        app_plans = (AppPlan *)palloc(sizeof(AppPlan) * app_plans_allocated);
    }
    else if (app_plans_size > app_plans_allocated) {
        app_plans_allocated = app_plans_allocated * 2;
        app_plans = (AppPlan *)repalloc(app_plans,
                                        sizeof(AppPlan) * app_plans_allocated);
    }
    if (!app_plans) {
        MemoryContextSwitchTo(tx_mctx);
        ereport(WARNING, errmsg("could not allocate memory for prepare plans"));
        goto fail;
    }

    WASM2PGFunc *wasm_to_pg_funcs =
        (WASM2PGFunc *)palloc(sizeof(WASM2PGFunc) * nargs);
    if (!wasm_to_pg_funcs) {
        MemoryContextSwitchTo(tx_mctx);
        ereport(WARNING,
                errmsg("could not allocate memory for wasm_to_pg_funcs"));
        goto fail;
    }

    MemoryContextSwitchTo(tx_mctx);

    WASMStructType *query_args_struct_type;
    if (!(query_args_struct_type =
              get_query_args_struct_type(exec_env, sql_idx))) {
        goto fail;
    }

    if (nargs > query_args_struct_type->field_count) {
        ereport(WARNING,
                (errmsg("query args count mismatch, expected: %d, got: %d",
                        nargs,
                        query_args_struct_type->field_count)));
        goto fail;
    }

    Context *ctx = wasm_runtime_get_user_data(exec_env);

    Oid *pg_argtypes = (Oid *)palloc(sizeof(Oid) * nargs);
    for (uint32 i = 0; i < nargs; i++) {
        Oid pg_argtype = 0;
        for (uint32 j = 0; j < 4; j++) {
            pg_argtype |= (Oid)arg_type_oids->elem_data[i * 4 + j] << j * 8;
        }
        pg_argtypes[i] = pg_argtype;

        uint32 field_type_idx =
            wasm_struct_type_get_field_type(query_args_struct_type, i, false)
                .heap_type;
        if (ctx->as_datum == field_type_idx) {
            wasm_to_pg_funcs[i] = wasm_as_datum_to_pg_value;
            continue;
        }

        uint8 wasm_argtype = query_args_struct_type->fields[i].field_type;
        switch (pg_argtype) {
            case BOOLOID:
                if (wasm_argtype == VALUE_TYPE_I32) {
                    wasm_to_pg_funcs[i] = wasm_i32_to_pg_bool;
                    break;
                }
                ereport(
                    WARNING,
                    (errmsg("expected wasm i32 for pg bool conversion, got %d",
                            wasm_argtype)));
                goto fail;
            case INT4OID:
                if (wasm_argtype == VALUE_TYPE_I32) {
                    wasm_to_pg_funcs[i] = wasm_i32_to_pg_int32;
                    break;
                }
                ereport(
                    WARNING,
                    (errmsg(
                        "expected wasm i32 for pg integer conversion, got %d",
                        wasm_argtype)));
                goto fail;
            case INT8OID:
                if (wasm_argtype == VALUE_TYPE_I64) {
                    wasm_to_pg_funcs[i] = wasm_i64_to_pg_int64;
                }
                else if (wasm_argtype == VALUE_TYPE_I32) {
                    wasm_to_pg_funcs[i] = wasm_i32_to_pg_int32;
                }
                else {
                    ereport(WARNING,
                            (errmsg("expected wasm i32 or i64 for pg bigint "
                                    "conversion, got %d",
                                    wasm_argtype)));
                    goto fail;
                }
                break;
            case TEXTOID:
            case VARCHAROID:
                if (wasm_is_type_multi_byte_type(wasm_argtype)) {
                    wasm_to_pg_funcs[i] = wasm_bytes_to_pg_text;
                    break;
                }
                ereport(WARNING,
                        (errmsg("expected wasm multi-byte type for pg text "
                                "conversion, got %d",
                                wasm_argtype)));
                goto fail;
            case TIMESTAMPOID:
                if (wasm_is_type_multi_byte_type(wasm_argtype)) {
                    wasm_to_pg_funcs[i] = wasm_bytest_to_pg_timestamp;
                    break;
                }
                ereport(WARNING,
                        (errmsg("expected wasm multi-byte type for pg timstamp "
                                "conversion, got %d",
                                wasm_argtype)));
                goto fail;
            default:
                ereport(WARNING, (errmsg("unsupported OID %d", pg_argtype)));
                goto fail;
        }
    }

    debug_query_string = sql;
    SPIPlanPtr plan = SPI_prepare(sql, nargs, pg_argtypes);

    if (!plan) {
        ereport(WARNING,
                (errmsg("could not prepare SPI plan: %s",
                        SPI_result_code_string(SPI_result))));
        goto fail;
    }

    ListCell *lc;
    TupleDesc tupdesc = NULL;
    foreach (lc, SPI_plan_get_plan_sources(plan)) {
        CachedPlanSource *plansource = (CachedPlanSource *)lfirst(lc);
        if (plansource && plansource->resultDesc) {
            tupdesc = plansource->resultDesc;
            break;
        }
    }
    if (tupdesc == NULL) {
        ereport(WARNING, (errmsg("no result description available")));
        goto fail;
    }

    WASMStructType *row_struct_type;
    if (!(row_struct_type = get_ret_row_struct_type(exec_env, sql_idx))) {
        goto fail;
    }

    if (tupdesc->natts > row_struct_type->field_count) {
        ereport(
            WARNING,
            (errmsg(
                "query result attribute count mismatch, expected: %d, got: %d",
                row_struct_type->field_count,
                tupdesc->natts)));
        goto fail;
    }

    tx_mctx = MemoryContextSwitchTo(TopMemoryContext);
    PG2WASMFunc *pg_to_wasm_funcs =
        (PG2WASMFunc *)palloc(sizeof(PG2WASMFunc) * tupdesc->natts);
    if (!pg_to_wasm_funcs) {
        MemoryContextSwitchTo(tx_mctx);
        ereport(WARNING,
                errmsg("could not allocate memory for pg_to_wasm_funcs"));
        goto fail;
    }
    MemoryContextSwitchTo(tx_mctx);

    for (uint32 i = 0; i < tupdesc->natts; i++) {
        uint32 field_type_idx =
            wasm_struct_type_get_field_type(row_struct_type, i, false)
                .heap_type;
        if (is_as_datum_impl(exec_env, field_type_idx)) {
            pg_to_wasm_funcs[i] = pg_value_to_wasm_as_datum;
            continue;
        }

        Oid pg_valtype = SPI_gettypeid(tupdesc, i + 1);
        uint8 wasm_valtype = row_struct_type->fields[i].field_type;
        switch (pg_valtype) {
            case BOOLOID:
                if (wasm_valtype == VALUE_TYPE_I32) {
                    pg_to_wasm_funcs[i] = pg_bool_to_wasm_i32;
                    break;
                }
                ereport(
                    WARNING,
                    (errmsg("expected wasm i32 for pg bool conversion, got %d",
                            wasm_valtype)));
                goto fail;
            case INT4OID:
                if (wasm_valtype == VALUE_TYPE_I32) {
                    pg_to_wasm_funcs[i] = pg_int32_to_wasm_i32;
                    break;
                }
                ereport(WARNING,
                        (errmsg("expected wasm i32 for pg integer "
                                "conversion, got %d",
                                wasm_valtype)));
                goto fail;
            case INT8OID:
                if (wasm_valtype == VALUE_TYPE_I64) {
                    pg_to_wasm_funcs[i] = pg_int64_to_wasm_i64;
                }
                else if (wasm_valtype == VALUE_TYPE_I32) {
                    pg_to_wasm_funcs[i] = pg_int32_to_wasm_i32;
                }
                else {
                    ereport(WARNING,
                            (errmsg("expected wasm i32 or i64 for pg bigint "
                                    "conversion, got %d",
                                    wasm_valtype)));
                    goto fail;
                }
                break;
            case TEXTOID:
            case VARCHAROID:
                if (wasm_is_type_multi_byte_type(wasm_valtype)) {
                    pg_to_wasm_funcs[i] = pg_text_to_wasm_bytes;
                    break;
                }
                ereport(WARNING,
                        (errmsg("expected wasm multi-byte type for pg text "
                                "conversion, got %d",
                                wasm_valtype)));
                goto fail;
            case TIMESTAMPOID:
                if (wasm_is_type_multi_byte_type(wasm_valtype)) {
                    pg_to_wasm_funcs[i] = pg_timestamp_to_wasm_bytes;
                    break;
                }
                ereport(WARNING,
                        (errmsg("expected wasm multi-byte type for pg timstamp "
                                "conversion, got %d",
                                wasm_valtype)));
                goto fail;
            default:
                ereport(WARNING, (errmsg("unsupported OID %d", pg_valtype)));
                break;
        }
    }

    app_plans[sql_idx].plan = plan;
    app_plans[sql_idx].wasm_to_pg_funcs = wasm_to_pg_funcs;
    app_plans[sql_idx].pg_to_wasm_funcs = pg_to_wasm_funcs;
    app_plans[sql_idx].nargs = nargs;
    app_plans[sql_idx].nattrs = tupdesc->natts;
    app_plans[sql_idx].sql = sql;

    if (SPI_keepplan(plan)) {
        ereport(WARNING, (errmsg("failed to keep plan")));
        goto fail;
    }

    debug_query_string = NULL;
    return sql_idx;

fail:
    debug_query_string = NULL;
    return -1;
}

int32_t
env_execute_statement(wasm_exec_env_t exec_env, int32_t idx) {
    ereport(DEBUG1, (errmsg("execute sql: #%d", idx)));
    wasm_val_t queries[1];

    Context *ctx = wasm_runtime_get_user_data(exec_env);
    wasm_function_inst_t func_inst = ctx->get_queries;
    if (!wasm_runtime_call_wasm_a(exec_env, func_inst, 1, queries, 0, NULL)) {
        ereport(WARNING, (errmsg("failed to get query args for sql #%d", idx)));
        return -1;
    }

    // Query[ARGS, RET]
    WASMValue query;
    wasm_struct_obj_get_field((WASMStructObjectRef)queries[0].of.foreign,
                              idx,
                              false,
                              &query);

    WASMValue args;
    wasm_struct_obj_get_field(query.data, 0, false, &args);

    AppPlan app_plan = app_plans[idx];
    Datum values[app_plan.nargs];
    for (uint32 i = 0; i < app_plan.nargs; i++) {
        wasm_value_t value;
        wasm_struct_obj_get_field(args.data, i, false, &value);
        values[i] = app_plan.wasm_to_pg_funcs[i](exec_env, value);
    }
    SPI_execute_plan(app_plan.plan, values, NULL, false, 0);

    uint32 tuptable_idx = tuptables_size++;
    if (!tuptables) {
        tuptables = (SPITupleTable **)palloc(sizeof(SPITupleTable *)
                                             * tuptables_allocated);
    }
    else if (tuptables_size > tuptables_allocated) {
        tuptables_allocated = tuptables_allocated * 2;
        tuptables = (SPITupleTable **)repalloc(tuptables,
                                               sizeof(SPITupleTable *)
                                                   * tuptables_allocated);
    }
    if (!tuptables) {
        ereport(WARNING, errmsg("could not allocate memory for tuple tables"));
        return -1;
    }

    // $TupleTable[T]
    WASMStructType *tuptable_struct_type;
    if (!(tuptable_struct_type = get_query_ret_struct_type(exec_env, idx))) {
        return -1;
    }

    WASMStructObjectRef tuptable_struct_ref =
        wasm_struct_obj_new_with_type(exec_env, tuptable_struct_type);
    wasm_value_t query_ret_value;
    query_ret_value.gc_obj = (wasm_obj_t)tuptable_struct_ref;
    wasm_struct_obj_set_field(query.data, 1, &query_ret_value);

    wasm_obj_set_gc_finalizer(exec_env,
                              (wasm_obj_t)tuptable_struct_ref,
                              tuple_table_finalizer,
                              (void *)(uintptr_t)tuptable_idx);

    WASMModuleCommon *module = wasm_exec_env_get_module(exec_env);

    // tuple table idx
    wasm_value_t tuptable_idx_value;
    tuptable_idx_value.i32 = tuptable_idx;
    wasm_struct_obj_set_field(tuptable_struct_ref, 0, &tuptable_idx_value);

    tuptables[tuptable_idx] = SPI_tuptable;

    // $@moonbitlang/core/builtin.Array<T> - struct
    wasm_ref_type_t rows_struct_ref_type =
        wasm_struct_type_get_field_type(tuptable_struct_type, 1, false);
    WASMStructType *rows_struct_type =
        (WASMStructType *)wasm_get_defined_type(module,
                                                rows_struct_ref_type.heap_type);
    Assert(rows_struct_type->base_type.type_flag == WASM_TYPE_STRUCT);

    WASMStructObjectRef rows_struct_ref =
        wasm_struct_obj_new_with_typeidx(exec_env,
                                         rows_struct_ref_type.heap_type);
    wasm_value_t rows_struct_value;
    rows_struct_value.gc_obj = (WASMObject *)rows_struct_ref;
    wasm_struct_obj_set_field(tuptable_struct_ref, 1, &rows_struct_value);

    // $FixedArray<UnsafeMaybeUninit<T>>
    wasm_ref_type_t rows_ref_type =
        wasm_struct_type_get_field_type(rows_struct_type, 0, false);
    WASMArrayType *rows_type =
        (WASMArrayType *)wasm_get_defined_type(module, rows_ref_type.heap_type);
    Assert(rows_type->base_type.type_flag == WASM_TYPE_ARRAY);

    WASMArrayObjectRef rows_ref =
        wasm_array_obj_new_with_typeidx(exec_env,
                                        rows_ref_type.heap_type,
                                        SPI_tuptable->numvals,
                                        NULL);
    wasm_value_t rows_value;
    rows_value.gc_obj = (WASMObject *)rows_ref;
    wasm_struct_obj_set_field(rows_struct_ref, 0, &rows_value);

    wasm_value_t rows_num_value;
    rows_num_value.i32 = SPI_tuptable->numvals;
    wasm_struct_obj_set_field(rows_struct_ref, 1, &rows_num_value);

    // T
    wasm_ref_type_t row_struct_ref_type =
        wasm_array_type_get_elem_type(rows_type, false);
    WASMStructType *row_struct_type =
        (WASMStructType *)wasm_get_defined_type(module,
                                                row_struct_ref_type.heap_type);

    bool isnull;
    TupleDesc tupdesc = SPI_tuptable->tupdesc;
    for (uint32 i = 0; i < SPI_tuptable->numvals; i++) {
        HeapTuple tuple = SPI_tuptable->vals[i];

        WASMStructObjectRef row_ref_type =
            wasm_struct_obj_new_with_typeidx(exec_env,
                                             row_struct_ref_type.heap_type);
        wasm_value_t row_value;
        row_value.gc_obj = (WASMObject *)row_ref_type;
        wasm_array_obj_set_elem(rows_ref, i, &row_value);

        for (uint32 j = 0; j < tupdesc->natts; j++) {
            Datum val = SPI_getbinval(tuple, tupdesc, j + 1, &isnull);
            wasm_ref_type_t col_ref_type =
                wasm_struct_type_get_field_type(row_struct_type, j, false);
            wasm_value_t col_value =
                app_plan.pg_to_wasm_funcs[j](val,
                                             tuptable_struct_ref,
                                             i,
                                             j,
                                             exec_env,
                                             col_ref_type.heap_type);
            wasm_struct_obj_set_field(row_ref_type, j, &col_value);
        }
    }

    return 1;
}

WASMArrayObjectRef
env_detoast(wasm_exec_env_t exec_env,
            int32_t tuptable_idx,
            int32_t row,
            int32_t col) {
    Datum pg_value = get_tuple_table_value(tuptable_idx, row, col);
    struct varlena *pg_var = PG_DETOAST_DATUM_PACKED(pg_value);
    uint32_t size = VARSIZE_ANY(pg_var);
    // TODO: Replace 2 with the type index for the bytes type
    WASMArrayObjectRef buf =
        wasm_array_obj_new_with_typeidx(exec_env, 2, size, NULL);
    char *view = wasm_array_obj_first_elem_addr(buf);
    memcpy(view, pg_var, size);
    return buf;
}
