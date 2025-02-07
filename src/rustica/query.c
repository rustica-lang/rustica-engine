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
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "tcop/utility.h"

#include "wasm_runtime_common.h"

#include "rustica/module.h"
#include "rustica/query.h"

static Datum
get_tuple_table_value(Context *ctx, int32_t idx, int32_t row, int32_t col) {
    if (idx < 0 || idx > list_length(ctx->tuple_tables))
        ereport(ERROR, errmsg("tuple table idx #%d is out of bounds", idx));

    SPITupleTable *tuptable = lfirst(list_nth_cell(ctx->tuple_tables, idx));
    if (tuptable == NULL)
        ereport(
            ERROR,
            errmsg("tuple table at idx #%d is already freed or does not exist",
                   idx));

    if (row < 0 || row >= tuptable->numvals)
        ereport(ERROR, errmsg("row idx #%d is out of bounds", row));

    bool isnull;
    HeapTuple tuple = tuptable->vals[row];
    Datum pg_value = SPI_getbinval(tuple, tuptable->tupdesc, col + 1, &isnull);
    if (pg_value == (Datum)NULL)
        ereport(
            ERROR,
            errmsg("could not get value at row #%d, column #%d", row, col + 1));

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

    // Call call_as_datum()
    wasm_val_t args[1] = { { .kind = WASM_EXTERNREF,
                             .of.foreign = (uintptr_t)value.gc_obj } };
    wasm_val_t result;
    wasm_runtime_call_wasm_a(exec_env, ctx->call_as_datum, 1, &result, 1, args);
    wasm_struct_obj_t datum_val = (wasm_struct_obj_t)result.of.foreign;

    // Unpack Ref[DatumEnum], a.k.a. Datum
    wasm_value_t val;
    wasm_struct_obj_get_field(datum_val, 0, false, &val);
    datum_val = (wasm_struct_obj_t)val.gc_obj;

    // The first field is the enum type
    wasm_value_t enum_type;
    wasm_struct_obj_get_field(datum_val, 0, false, &enum_type);
    wasm_struct_obj_get_field(datum_val, 1, false, &val);
    switch (enum_type.i32) {
        // Value(UInt64)
        case 0:
            return UInt64GetDatum(val.u64);

        // Owned(Bytes)
        case 1:
            return PointerGetDatum(
                wasm_array_obj_first_elem_addr((wasm_array_obj_t)val.gc_obj));

        // Ref(RawDatumRef)
        case 2:
        {
            wasm_struct_obj_t raw_datum_ref = (wasm_struct_obj_t)val.gc_obj;
            wasm_value_t tuple_table_idx;
            wasm_struct_obj_get_field(raw_datum_ref,
                                      0,
                                      false,
                                      &tuple_table_idx);
            wasm_value_t row;
            wasm_struct_obj_get_field(raw_datum_ref, 1, false, &row);
            wasm_value_t col;
            wasm_struct_obj_get_field(raw_datum_ref, 2, false, &col);

            return get_tuple_table_value(ctx,
                                         tuple_table_idx.i32,
                                         row.i32,
                                         col.i32);
        }

        default:
            ereport(ERROR, errmsg("unknown DatumEnum value"));
    }
}

static RST_WASM_TO_PG_RET (*wasm_to_pg_funcs[])(RST_WASM_TO_PG_ARGS) = {
    wasm_i32_to_pg_bool,         wasm_i32_to_pg_int32,
    wasm_i64_to_pg_int64,        wasm_bytes_to_pg_text,
    wasm_bytest_to_pg_timestamp, wasm_as_datum_to_pg_value,
};

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
        wasm_array_obj_new_with_typeidx(exec_env, type.heap_type, size, NULL);
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
        wasm_array_obj_new_with_typeidx(exec_env, type.heap_type, size, NULL);
    char *data_ptr = wasm_array_obj_first_elem_addr(array_obj);
    memcpy(data_ptr, data, size);
    wasm_value.gc_obj = (wasm_obj_t)array_obj;
    return wasm_value;
}

static RST_PG_TO_WASM_RET
pg_value_to_wasm_as_datum(RST_PG_TO_WASM_ARGS) {
    wasm_value_t wasm_value;
    WASMStructObjectRef datum_obj =
        wasm_struct_obj_new_with_typeidx(exec_env, type.heap_type);

    wasm_value_t field_val;
    field_val.gc_obj = (wasm_obj_t)tuptable;
    wasm_struct_obj_set_field(datum_obj, 1, &field_val);
    field_val.i32 = (int32)row;
    wasm_struct_obj_set_field(datum_obj, 2, &field_val);
    field_val.i32 = (int32)col;
    wasm_struct_obj_set_field(datum_obj, 3, &field_val);

    wasm_value.gc_obj = (wasm_obj_t)datum_obj;
    return wasm_value;
}

static RST_PG_TO_WASM_RET (*pg_to_wasm_funcs[])(RST_PG_TO_WASM_ARGS) = {
    pg_bool_to_wasm_i32,        pg_int32_to_wasm_i32,
    pg_int64_to_wasm_i64,       pg_text_to_wasm_bytes,
    pg_timestamp_to_wasm_bytes, pg_value_to_wasm_as_datum,
};

static void
tuple_table_finalizer(const wasm_obj_t obj, void *data) {
    Context *ctx = (Context *)data;
    wasm_struct_obj_t tuptable = (wasm_struct_obj_t)obj;
    wasm_value_t val;
    wasm_struct_obj_get_field(tuptable, 0, false, &val);
    if (val.i32 >= 0 && val.i32 < list_length(ctx->tuple_tables)) {
        ListCell *cell = list_nth_cell(ctx->tuple_tables, val.i32);
        SPI_freetuptable((SPITupleTable *)lfirst(cell));
        lfirst(cell) = NULL;
    }
}

void
rst_free_query_plan(QueryPlan *plan) {
    if (!plan)
        return;
    if (plan->plan) {
        ereport(DEBUG2, errmsg("SPI_freeplan"));
        SPI_freeplan(plan->plan);
    }
    if (plan->wasm_to_pg_funcs)
        pfree(plan->wasm_to_pg_funcs);
    // GOTCHA: plan->pg_to_wasm_funcs and plan->ret_field_types lives
    // in the same memory allocation.
}

void
rst_init_query_plan(QueryPlan *plan, HeapTuple query_tup, TupleDesc tupdesc) {
    bool isnull;
    Datum datum;

    // Take the SQL text
    datum = SPI_getbinval(query_tup, tupdesc, 3, &isnull);
    Assert(!isnull);
    text *sql_text = DatumGetTextPP(datum);
    char *sql = VARDATA_ANY(sql_text);

    debug_query_string = sql;
    PG_TRY();
    {
        // Take argument OIDs
        datum = SPI_getbinval(query_tup, tupdesc, 5, &isnull);
        Assert(!isnull);
        int nargs;
        Datum *datum_array;
        deconstruct_array(DatumGetArrayTypeP(datum),
                          INT4OID,
                          sizeof(int32),
                          true,
                          'i',
                          &datum_array,
                          NULL,
                          &nargs);
        plan->nargs = nargs;
        Oid argtypes[nargs];
        for (int i = 0; i < nargs; i++) {
            argtypes[i] = DatumGetInt32(datum_array[i]);
        }
        pfree(datum_array);

        // Create SPI query plan
        SPIPlanPtr spi_plan = SPI_prepare(sql, (int)nargs, argtypes);
        if (!spi_plan)
            ereport(ERROR,
                    errmsg("failed to prepare statement: %s",
                           SPI_result_code_string(SPI_result)));
        if (SPI_keepplan(spi_plan))
            ereport(ERROR, errmsg("failed to keep plan"));
        plan->plan = spi_plan;
        List *source_list = SPI_plan_get_plan_sources(spi_plan);
        Assert(list_length(source_list) == 1);
        CachedPlanSource *source = ((CachedPlanSource *)linitial(source_list));
        int nattrs = 0;
        if (source->resultDesc)
            nattrs = source->resultDesc->natts;

        // Prepare argument converters
        datum = SPI_getbinval(query_tup, tupdesc, 7, &isnull);
        Assert(!isnull);
        int len;
        deconstruct_array(DatumGetArrayTypeP(datum),
                          INT4OID,
                          sizeof(int32),
                          true,
                          'i',
                          &datum_array,
                          NULL,
                          &len);
        if (len != nargs)
            ereport(
                ERROR,
                errmsg(
                    "arg_field_fn has different length (%d) than arg_oids (%d)",
                    len,
                    nargs));
        plan->wasm_to_pg_funcs = (WASM2PGFunc *)MemoryContextAlloc(
            TopMemoryContext,
            sizeof(void *) * (nargs + nattrs)
                + sizeof(wasm_ref_type_t) * nattrs);
        if (nattrs > 0) {
            plan->pg_to_wasm_funcs =
                (PG2WASMFunc *)(plan->wasm_to_pg_funcs + nargs);
            plan->ret_field_types =
                (wasm_ref_type_t *)(plan->wasm_to_pg_funcs + nargs + nattrs);
        }
        for (int i = 0; i < nargs; i++) {
            plan->wasm_to_pg_funcs[i] =
                wasm_to_pg_funcs[DatumGetInt32(datum_array[i])];
        }
        pfree(datum_array);

        // Take result types
        if (nattrs > 0) {
            datum = SPI_getbinval(query_tup, tupdesc, 8, &isnull);
            Assert(!isnull);
            wasm_ref_type_t *ref_type_ptr;
            deconstruct_array(DatumGetArrayTypeP(datum),
                              INT8OID,
                              sizeof(int64),
                              true,
                              'i',
                              (Datum **)&ref_type_ptr,
                              NULL,
                              &len);
            if (len != 4)
                ereport(ERROR, errmsg("wrong number of ret_type elements"));
            plan->tuptable_type = ref_type_ptr[0];
            plan->array_type = ref_type_ptr[1];
            plan->fixed_array_type = ref_type_ptr[2];
            plan->ret_type = ref_type_ptr[3];
            pfree(ref_type_ptr);

            // Take ref types of result fields
            datum = SPI_getbinval(query_tup, tupdesc, 10, &isnull);
            Assert(!isnull);
            deconstruct_array(DatumGetArrayTypeP(datum),
                              INT8OID,
                              sizeof(int64),
                              true,
                              'i',
                              (Datum **)&ref_type_ptr,
                              NULL,
                              &len);
            if (len != nattrs)
                ereport(ERROR,
                        errmsg("ret_field_types has different length (%d) than "
                               "described (%d)",
                               len,
                               nattrs));
            memcpy(plan->ret_field_types,
                   ref_type_ptr,
                   sizeof(wasm_ref_type_t) * len);
            pfree(ref_type_ptr);
        }

        // Prepare result field converters
        datum = SPI_getbinval(query_tup, tupdesc, 11, &isnull);
        Assert(!isnull);
        deconstruct_array(DatumGetArrayTypeP(datum),
                          INT4OID,
                          sizeof(int32),
                          true,
                          'i',
                          &datum_array,
                          NULL,
                          &len);
        if (len != nattrs)
            ereport(ERROR,
                    errmsg("ret_field_fn has different length (%d) than "
                           "described (%d)",
                           len,
                           nattrs));
        for (int i = 0; i < nattrs; i++) {
            plan->pg_to_wasm_funcs[i] =
                pg_to_wasm_funcs[DatumGetInt32(datum_array[i])];
        }
        pfree(datum_array);
        plan->nattrs = nattrs;
    }
    PG_FINALLY();
    { debug_query_string = NULL; }
    PG_END_TRY();
}

void
rst_init_instance_context(wasm_exec_env_t exec_env) {
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    wasm_module_inst_t instance = wasm_exec_env_get_module_inst(exec_env);
    wasm_function_inst_t func;
    if ((func = wasm_runtime_lookup_function(instance, "call_as_datum"))) {
        ctx->call_as_datum = func;
    }
    if ((func = wasm_runtime_lookup_function(instance, "get_queries"))) {
        wasm_val_t val;
        wasm_val_t args[1] = { { .kind = WASM_I32, .of.i32 = 0 } };
        if (!wasm_runtime_call_wasm_a(exec_env, func, 1, &val, 1, args))
            ereport(ERROR, errmsg("failed to call get_queries()"));
        Assert(val.kind == WASM_EXTERNREF);
        ctx->queries = (wasm_struct_obj_t)val.of.ref;
        Assert(pmod->nqueries == wasm_struct_obj_get_field_count(ctx->queries));
        for (int i = 0; i < ctx->module->nqueries; i++) {
            wasm_value_t value;
            wasm_struct_obj_get_field(ctx->queries, i, false, &value);
            wasm_struct_obj_t query = (wasm_struct_obj_t)value.gc_obj;
            Assert(wasm_obj_is_struct_obj(query));
            value.i64 = i;
            wasm_struct_obj_set_field(query, 0, &value);
        }
    }
}

void
rst_free_instance_context(wasm_exec_env_t exec_env) {
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    ListCell *cell;
    foreach (cell, ctx->tuple_tables) {
        SPI_freetuptable((SPITupleTable *)lfirst(cell));
    }
    list_free(ctx->tuple_tables);
}

int32_t
env_execute_statement(wasm_exec_env_t exec_env, int32_t idx) {
    ereport(DEBUG1, (errmsg("execute sql: #%d", idx)));

    // Take out the QueryPlan
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    if (idx < 0 || idx >= ctx->module->nqueries)
        ereport(ERROR, errmsg("no such query: #%d", idx));
    QueryPlan *plan = ctx->module->queries + idx;

    // Read out user's query arguments
    wasm_value_t val;
    wasm_struct_obj_get_field(ctx->queries, idx, false, &val);
    wasm_struct_obj_t query = (wasm_struct_obj_t)val.gc_obj;
    wasm_struct_obj_get_field(query, 3, false, &val);
    wasm_struct_obj_t args = (wasm_struct_obj_t)val.gc_obj;

    // Execute the query
    Datum values[plan->nargs];
    for (uint32 i = 0; i < plan->nargs; i++) {
        wasm_struct_obj_get_field(args, i, false, &val);
        values[i] = plan->wasm_to_pg_funcs[i](exec_env, val);
    }
    SPI_execute_plan(plan->plan, values, NULL, false, 0);

    if (plan->nattrs) {
        // Prepare the TupleTable[T] struct object
        wasm_struct_obj_t tuptable =
            wasm_struct_obj_new_with_typeidx(exec_env,
                                             plan->tuptable_type.heap_type);
        wasm_value_t query_ret_value = { .gc_obj = (wasm_obj_t)tuptable };
        wasm_struct_obj_set_field(query, 4, &query_ret_value);

        // Remember the tuple table and setup gc
        int tuptable_idx = list_length(ctx->tuple_tables);
        wasm_value_t tuptable_idx_value = { .i32 = tuptable_idx };
        wasm_struct_obj_set_field(tuptable, 0, &tuptable_idx_value);
        wasm_obj_set_gc_finalizer(exec_env,
                                  (wasm_obj_t)tuptable,
                                  tuple_table_finalizer,
                                  ctx);
        MemoryContext tx_mctx = MemoryContextSwitchTo(TopMemoryContext);
        PG_TRY();
        { ctx->tuple_tables = lappend(ctx->tuple_tables, SPI_tuptable); }
        PG_FINALLY();
        { MemoryContextSwitchTo(tx_mctx); }
        PG_END_TRY();

        // $@moonbitlang/core/builtin.Array<T> - struct
        wasm_struct_obj_t rows_struct =
            wasm_struct_obj_new_with_typeidx(exec_env,
                                             plan->array_type.heap_type);
        wasm_value_t rows_struct_value = { .gc_obj = (wasm_obj_t)rows_struct };
        wasm_struct_obj_set_field(tuptable, 1, &rows_struct_value);

        // $FixedArray<UnsafeMaybeUninit<T>>
        wasm_array_obj_t rows_arr =
            wasm_array_obj_new_with_typeidx(exec_env,
                                            plan->fixed_array_type.heap_type,
                                            SPI_tuptable->numvals,
                                            NULL);
        wasm_value_t rows_value = { .gc_obj = (wasm_obj_t)rows_arr };
        wasm_struct_obj_set_field(rows_struct, 0, &rows_value);
        wasm_value_t rows_num_value = { .i32 = (int32)SPI_tuptable->numvals };
        wasm_struct_obj_set_field(rows_struct, 1, &rows_num_value);

        // Fill each element in the array
        bool isnull;
        TupleDesc tupdesc = SPI_tuptable->tupdesc;
        for (uint32 i = 0; i < SPI_tuptable->numvals; i++) {
            HeapTuple tuple = SPI_tuptable->vals[i];

            // T
            wasm_struct_obj_t row =
                wasm_struct_obj_new_with_typeidx(exec_env,
                                                 plan->ret_type.heap_type);
            wasm_value_t row_value = { .gc_obj = (wasm_obj_t)row };
            wasm_array_obj_set_elem(rows_arr, i, &row_value);

            for (uint32 j = 0; j < plan->nattrs; j++) {
                Datum binval =
                    SPI_getbinval(tuple, tupdesc, (int)j + 1, &isnull);
                wasm_value_t col_value =
                    plan->pg_to_wasm_funcs[j](binval,
                                              tuptable,
                                              i,
                                              j,
                                              exec_env,
                                              plan->ret_field_types[j]);
                wasm_struct_obj_set_field(row, j, &col_value);
            }
        }
    }

    return 1;
}

WASMArrayObjectRef
env_detoast(wasm_exec_env_t exec_env,
            int32_t tuptable_idx,
            int32_t row,
            int32_t col) {
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    Datum pg_value = get_tuple_table_value(ctx, tuptable_idx, row, col);
    struct varlena *pg_var = PG_DETOAST_DATUM_PACKED(pg_value);
    uint32_t size = VARSIZE_ANY(pg_var);
    WASMArrayObjectRef buf =
        wasm_array_obj_new_with_typeidx(exec_env,
                                        ctx->module->heap_types.bytes,
                                        size,
                                        NULL);
    char *view = wasm_array_obj_first_elem_addr(buf);
    memcpy(view, VARDATA_ANY(pg_var), size);
    if ((void *)pg_var != DatumGetPointer(pg_value))
        pfree(pg_var);
    return buf;
}
