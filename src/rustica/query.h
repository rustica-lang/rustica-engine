/*
 * Copyright (c) 2024-present 燕几（北京）科技有限公司
 *
 * Rustica (runtime) is licensed under Mulan PSL v2. You can use this
 * software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *              https://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
 * OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the Mulan PSL v2 for more details.
 */

#ifndef RUSTICA_QUERY_H
#define RUSTICA_QUERY_H

#include "postgres.h"
#include "executor/spi.h"
#include "storage/latch.h"

#include "llhttp.h"

#include "wasm_runtime_common.h"

#define RST_WASM_TO_PG_ARGS \
    wasm_exec_env_t exec_env, Oid oid, const wasm_value_t value
#define RST_WASM_TO_PG_RET Datum
#define RST_PG_TO_WASM_ARGS                                               \
    Datum value, wasm_obj_t tuple_obj, Oid oid, wasm_exec_env_t exec_env, \
        wasm_ref_type_t type
#define RST_PG_TO_WASM_RET wasm_value_t

typedef struct PreparedModule PreparedModule;

typedef struct Context {
    WaitEventSet *wait_set;
    pgsocket fd;

    llhttp_t http_parser;
    llhttp_settings_t http_settings;
    wasm_obj_t current_buf;
    int32_t bytes_view;
    wasm_function_inst_t on_message_begin;
    wasm_function_inst_t on_method;
    wasm_function_inst_t on_method_complete;
    wasm_function_inst_t on_url;
    wasm_function_inst_t on_url_complete;
    wasm_function_inst_t on_version;
    wasm_function_inst_t on_version_complete;
    wasm_function_inst_t on_header_field;
    wasm_function_inst_t on_header_field_complete;
    wasm_function_inst_t on_header_value;
    wasm_function_inst_t on_header_value_complete;
    wasm_function_inst_t on_headers_complete;
    wasm_function_inst_t on_body;
    wasm_function_inst_t on_message_complete;
    wasm_function_inst_t on_error;

    PreparedModule *module;
    wasm_struct_obj_t queries;
    WASMRttTypeRef anyref_array;
    wasm_function_inst_t json_parse_push_string;
    wasm_function_inst_t json_parse_push_number;
    wasm_function_inst_t json_parse_push_bool;
    wasm_function_inst_t json_parse_push_null;
    wasm_function_inst_t json_parse_object_start;
    wasm_function_inst_t json_parse_object_field_start;
    wasm_function_inst_t json_parse_object_end;
    wasm_function_inst_t json_parse_array_start;
    wasm_function_inst_t json_parse_array_end;
} Context;

typedef RST_WASM_TO_PG_RET (*WASM2PGFunc)(RST_WASM_TO_PG_ARGS);
typedef RST_PG_TO_WASM_RET (*PG2WASMFunc)(RST_PG_TO_WASM_ARGS);

typedef struct QueryPlan {
    SPIPlanPtr plan;
    uint32 nargs;
    uint32 nattrs;
    Oid *argtypes;
    Oid *rettypes;
    wasm_ref_type_t ret_type;
    wasm_ref_type_t *ret_field_types;
    WASM2PGFunc *wasm_to_pg_funcs;
    PG2WASMFunc *pg_to_wasm_funcs;
} QueryPlan;

void
rst_init_query_plan(QueryPlan *plan, HeapTuple query_tup, TupleDesc tupdesc);

void
rst_free_query_plan(QueryPlan *plan);

void
rst_init_instance_context(wasm_exec_env_t exec_env);

void
rst_free_instance_context(wasm_exec_env_t exec_env);

void
rst_register_natives_query();

#endif /* RUSTICA_QUERY_H */
