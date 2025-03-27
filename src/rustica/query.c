/*
 * Copyright (c) 2024-present 燕几（北京）科技有限公司
 *
 * Rustica Engine is licensed under Mulan PSL v2. You can use this
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
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "tcop/utility.h"

#include "wasm_runtime_common.h"

#include "rustica/datatypes.h"
#include "rustica/module.h"
#include "rustica/query.h"

static RST_WASM_TO_PG_RET
wasm_i32_to_pg_bool(RST_WASM_TO_PG_ARGS) {
    PG_RETURN_BOOL(value.i32 ? true : false);
}

static RST_WASM_TO_PG_RET
wasm_i32_to_pg_int4(RST_WASM_TO_PG_ARGS) {
    PG_RETURN_INT32(value.i32);
}

static RST_WASM_TO_PG_RET
wasm_i64_to_pg_int8(RST_WASM_TO_PG_ARGS) {
    PG_RETURN_INT64(value.i64);
}

static RST_WASM_TO_PG_RET
wasm_f32_to_pg_float4(RST_WASM_TO_PG_ARGS) {
    PG_RETURN_FLOAT4(value.f32);
}

static RST_WASM_TO_PG_RET
wasm_f64_to_pg_float8(RST_WASM_TO_PG_ARGS) {
    PG_RETURN_FLOAT8(value.f64);
}

static RST_WASM_TO_PG_RET
wasm_externref_to_datum_obj(RST_WASM_TO_PG_ARGS) {
    return wasm_externref_obj_get_datum(value.gc_obj, oid);
}

static RST_WASM_TO_PG_RET
wasm_i32_array_to_pg_int2_array(RST_WASM_TO_PG_ARGS) {
    wasm_value_t val;
    wasm_struct_obj_get_field((wasm_struct_obj_t)value.gc_obj, 0, false, &val);
    wasm_array_obj_t fixed_array = (wasm_array_obj_t)val.gc_obj;
    wasm_struct_obj_get_field((wasm_struct_obj_t)value.gc_obj, 1, false, &val);
    int len = val.i32;
    Datum *datum_array = palloc(sizeof(Datum) * len);
    for (int i = 0; i < len; i++) {
        wasm_array_obj_get_elem(fixed_array, i, false, &val);
        datum_array[i] = Int16GetDatum((int16_t)val.i32);
    }
    ArrayType *array =
        construct_array(datum_array, len, INT2OID, sizeof(int16), true, 's');
    PG_RETURN_ARRAYTYPE_P(array);
}

static RST_WASM_TO_PG_RET
wasm_i32_array_to_pg_int4_array(RST_WASM_TO_PG_ARGS) {
    wasm_value_t val;
    wasm_struct_obj_get_field((wasm_struct_obj_t)value.gc_obj, 0, false, &val);
    wasm_array_obj_t fixed_array = (wasm_array_obj_t)val.gc_obj;
    wasm_struct_obj_get_field((wasm_struct_obj_t)value.gc_obj, 1, false, &val);
    int len = val.i32;
    Datum *datum_array = palloc(sizeof(Datum) * len);
    for (int i = 0; i < len; i++) {
        wasm_array_obj_get_elem(fixed_array, i, false, &val);
        datum_array[i] = Int32GetDatum(val.i32);
    }
    ArrayType *array =
        construct_array(datum_array, len, INT4OID, sizeof(int32), true, 'i');
    PG_RETURN_ARRAYTYPE_P(array);
}

static RST_WASM_TO_PG_RET (*wasm_to_pg_funcs[])(RST_WASM_TO_PG_ARGS) = {
    wasm_i32_to_pg_bool,
    wasm_i32_to_pg_int4,
    wasm_i64_to_pg_int8,
    wasm_f32_to_pg_float4,
    wasm_f64_to_pg_float8,
    wasm_externref_to_datum_obj,
    wasm_i32_array_to_pg_int2_array,
    wasm_i32_array_to_pg_int4_array,
};

static RST_PG_TO_WASM_RET
pg_bool_to_wasm_i32(RST_PG_TO_WASM_ARGS) {
    wasm_value_t wasm_value = { .i32 = DatumGetBool(value) };
    return wasm_value;
}

static RST_PG_TO_WASM_RET
pg_int4_to_wasm_i32(RST_PG_TO_WASM_ARGS) {
    wasm_value_t wasm_value = { .i32 = DatumGetInt32(value) };
    return wasm_value;
}

static RST_PG_TO_WASM_RET
pg_int8_to_wasm_i64(RST_PG_TO_WASM_ARGS) {
    wasm_value_t wasm_value = { .i64 = DatumGetInt64(value) };
    return wasm_value;
}

static RST_PG_TO_WASM_RET
pg_float4_to_wasm_f32(RST_PG_TO_WASM_ARGS) {
    wasm_value_t wasm_value = { .f32 = DatumGetFloat4(value) };
    return wasm_value;
}

static RST_PG_TO_WASM_RET
pg_float8_to_wasm_f64(RST_PG_TO_WASM_ARGS) {
    wasm_value_t wasm_value = { .f64 = DatumGetFloat8(value) };
    return wasm_value;
}

static RST_PG_TO_WASM_RET
pg_datum_to_wasm_obj(RST_PG_TO_WASM_ARGS) {
    wasm_value_t wasm_value;
    obj_t obj = rst_obj_new(exec_env, OBJ_DATUM, tuptable, 0);
    obj->body.datum = value;
    obj->oid = oid;
    wasm_value.gc_obj = (wasm_obj_t)rst_externref_of_obj(exec_env, obj);
    return wasm_value;
}

static RST_PG_TO_WASM_RET
pg_int2_array_to_wasm_i32_array(RST_PG_TO_WASM_ARGS) {
    Datum *datum_array;
    bool *isnull;
    int len;
    deconstruct_array(DatumGetArrayTypeP(value),
                      INT2OID,
                      2,
                      true,
                      's',
                      &datum_array,
                      &isnull,
                      &len);

    wasm_local_obj_ref_t arr_struct_ref;
    wasm_struct_obj_t arr_struct =
        wasm_struct_obj_new_with_typeidx(exec_env, type.heap_type);
    wasm_runtime_push_local_obj_ref(exec_env, &arr_struct_ref);
    wasm_obj_t arr_obj = (wasm_obj_t)arr_struct;
    arr_struct_ref.val = arr_obj;

    wasm_struct_type_t struct_type =
        (wasm_struct_type_t)wasm_obj_get_defined_type(arr_obj);
    wasm_ref_type_t fixed_array_type =
        wasm_struct_type_get_field_type(struct_type, 0, NULL);
    wasm_array_obj_t fixed_array =
        wasm_array_obj_new_with_typeidx(exec_env,
                                        fixed_array_type.heap_type,
                                        len,
                                        NULL);

    wasm_value_t val = { .gc_obj = (wasm_obj_t)fixed_array };
    wasm_struct_obj_set_field(arr_struct, 0, &val);
    val.i32 = len;
    wasm_struct_obj_set_field(arr_struct, 1, &val);

    for (int i = 0; i < len; i++) {
        val.i32 = DatumGetInt16(datum_array[i]);
        wasm_array_obj_set_elem(fixed_array, i, &val);
    }

    wasm_runtime_pop_local_obj_ref(exec_env);
    val.gc_obj = arr_obj;
    return val;
}

static RST_PG_TO_WASM_RET
pg_int4_array_to_wasm_i32_array(RST_PG_TO_WASM_ARGS) {
    Datum *datum_array;
    bool *isnull;
    int len;
    deconstruct_array(DatumGetArrayTypeP(value),
                      INT4OID,
                      4,
                      true,
                      'i',
                      &datum_array,
                      &isnull,
                      &len);

    wasm_local_obj_ref_t arr_struct_ref;
    wasm_struct_obj_t arr_struct =
        wasm_struct_obj_new_with_typeidx(exec_env, type.heap_type);
    wasm_runtime_push_local_obj_ref(exec_env, &arr_struct_ref);
    wasm_obj_t arr_obj = (wasm_obj_t)arr_struct;
    arr_struct_ref.val = arr_obj;

    wasm_struct_type_t struct_type =
        (wasm_struct_type_t)wasm_obj_get_defined_type(arr_obj);
    wasm_ref_type_t fixed_array_type =
        wasm_struct_type_get_field_type(struct_type, 0, NULL);
    wasm_array_obj_t fixed_array =
        wasm_array_obj_new_with_typeidx(exec_env,
                                        fixed_array_type.heap_type,
                                        len,
                                        NULL);

    wasm_value_t val = { .gc_obj = (wasm_obj_t)fixed_array };
    wasm_struct_obj_set_field(arr_struct, 0, &val);
    val.i32 = len;
    wasm_struct_obj_set_field(arr_struct, 1, &val);

    for (int i = 0; i < len; i++) {
        val.i32 = DatumGetInt32(datum_array[i]);
        wasm_array_obj_set_elem(fixed_array, i, &val);
    }

    wasm_runtime_pop_local_obj_ref(exec_env);
    val.gc_obj = arr_obj;
    return val;
}

static RST_PG_TO_WASM_RET (*pg_to_wasm_funcs[])(RST_PG_TO_WASM_ARGS) = {
    pg_bool_to_wasm_i32,
    pg_int4_to_wasm_i32,
    pg_int8_to_wasm_i64,
    pg_float4_to_wasm_f32,
    pg_float8_to_wasm_f64,
    pg_datum_to_wasm_obj,
    pg_int2_array_to_wasm_i32_array,
    pg_int4_array_to_wasm_i32_array,
};

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
    // GOTCHA: plan->pg_to_wasm_funcs, plan->ret_field_types and plan->argtypes
    // all live in the same memory allocation.
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
        if (nargs + nattrs > 0) {
            plan->wasm_to_pg_funcs = (WASM2PGFunc *)MemoryContextAlloc(
                TopMemoryContext,
                sizeof(void *) * (nargs + nattrs)
                    + sizeof(wasm_ref_type_t) * nattrs
                    + sizeof(Oid) * (nargs + nattrs));
            if (nattrs > 0) {
                plan->pg_to_wasm_funcs =
                    (PG2WASMFunc *)(plan->wasm_to_pg_funcs + nargs);
                plan->ret_field_types =
                    (wasm_ref_type_t *)(plan->wasm_to_pg_funcs + nargs
                                        + nattrs);
                if (nargs > 0) {
                    plan->argtypes = (Oid *)(plan->ret_field_types + nattrs);
                    plan->rettypes = plan->argtypes + nargs;
                }
                else {
                    plan->rettypes = (Oid *)(plan->ret_field_types + nattrs);
                }
            }
            else if (nargs > 0) {
                plan->argtypes = (Oid *)(plan->wasm_to_pg_funcs + nargs);
            }
            for (int i = 0; i < nargs; i++) {
                plan->wasm_to_pg_funcs[i] =
                    wasm_to_pg_funcs[DatumGetInt32(datum_array[i])];
                plan->argtypes[i] = argtypes[i];
            }
        }
        pfree(datum_array);

        if (nattrs > 0) {
            // Take result types
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
            if (len != 3)
                ereport(ERROR, errmsg("wrong number of ret_type elements"));
            plan->array_type = ref_type_ptr[0];
            plan->fixed_array_type = ref_type_ptr[1];
            plan->ret_type = ref_type_ptr[2];
            pfree(ref_type_ptr);

            // Take result OIDs
            datum = SPI_getbinval(query_tup, tupdesc, 9, &isnull);
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
                        errmsg("ret_oids has different length (%d) than "
                               "described (%d)",
                               len,
                               nattrs));
            for (int i = 0; i < nattrs; i++) {
                plan->rettypes[i] = DatumGetInt32(datum_array[i]);
            }
            pfree(datum_array);

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
    {
        debug_query_string = NULL;
    }
    PG_END_TRY();
}

void
rst_init_instance_context(wasm_exec_env_t exec_env) {
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    wasm_module_inst_t instance = wasm_exec_env_get_module_inst(exec_env);
    wasm_function_inst_t func;
    if ((func = wasm_runtime_lookup_function(instance, "get_queries"))) {
        wasm_val_t val;
        wasm_val_t args[1] = { { .kind = WASM_I32, .of.i32 = 0 } };
        if (!wasm_runtime_call_wasm_a(exec_env, func, 1, &val, 1, args))
            ereport(ERROR, errmsg("failed to call get_queries()"));
        Assert(val.kind == WASM_EXTERNREF);
        ctx->queries = (wasm_struct_obj_t)val.of.ref;
        Assert(ctx->module->nqueries
               == wasm_struct_obj_get_field_count(ctx->queries));
        for (int i = 0; i < ctx->module->nqueries; i++) {
            wasm_value_t value;
            wasm_struct_obj_get_field(ctx->queries, i, false, &value);
            Assert(wasm_obj_is_struct_obj(value.gc_obj));
            wasm_struct_obj_t query = (wasm_struct_obj_t)value.gc_obj;
            value.i64 = i;
            wasm_struct_obj_set_field(query, 0, &value);
        }
    }
}

void
rst_free_instance_context(wasm_exec_env_t exec_env) {
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    pfree(ctx->anyref_array->defined_type);
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
        values[i] = plan->wasm_to_pg_funcs[i](exec_env, plan->argtypes[i], val);
    }
    SPI_execute_plan(plan->plan, values, NULL, false, 0);

    if (plan->nattrs) {
        obj_t obj = rst_obj_new(exec_env, OBJ_TUPLE_TABLE, NULL, 0);
        obj->flags |= OBJ_OWNS_BODY;
        obj->body.tuptable = SPI_tuptable;
        wasm_obj_t tuptable = rst_anyref_of_obj(exec_env, obj);
        wasm_local_obj_ref_t local_ref;
        wasm_runtime_push_local_obj_ref(exec_env, &local_ref);
        local_ref.val = tuptable;

        // $@moonbitlang/core/builtin.Array<T> - struct
        wasm_struct_obj_t rows_struct =
            wasm_struct_obj_new_with_typeidx(exec_env,
                                             plan->array_type.heap_type);
        wasm_value_t rows_struct_value = { .gc_obj = (wasm_obj_t)rows_struct };
        wasm_struct_obj_set_field(query, 4, &rows_struct_value);

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
                                              plan->rettypes[j],
                                              exec_env,
                                              plan->ret_field_types[j]);
                wasm_struct_obj_set_field(row, j, &col_value);
            }
        }

        // all values are copied, so we can free the tuptable early
        wasm_runtime_remove_local_obj_ref(exec_env, &local_ref);
    }

    return 1;
}
