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

#ifndef RUSTICA_QUERY_H
#define RUSTICA_QUERY_H

#include <sys/socket.h>

#include "postgres.h"
#include "executor/spi.h"
#include "storage/latch.h"

#include "llhttp.h"

#include "wasm_runtime_common.h"

typedef struct Context {
    WaitEventSet *wait_set;
    pgsocket fd;

    llhttp_t http_parser;
    llhttp_settings_t http_settings;
    WASMArrayObjectRef current_buf;
    int32_t bytes_view;
    int32_t bytes;
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

    int32_t as_datum;
    int32_t raw_datum;
    wasm_function_inst_t get_queries;
    wasm_function_inst_t as_raw_datum;
} Context;

typedef Datum (*WASM2PGFunc)(wasm_exec_env_t exec_env, wasm_value_t value);
typedef wasm_value_t (*PG2WASMFunc)(Datum value,
                                    WASMStructObjectRef tuptable,
                                    uint32 row,
                                    uint32 col,
                                    wasm_exec_env_t exec_env,
                                    uint32 type_idx);
typedef struct AppPlan {
    char *sql;
    SPIPlanPtr plan;
    uint32 nargs;
    uint32 nattrs;
    WASM2PGFunc *wasm_to_pg_funcs;
    PG2WASMFunc *pg_to_wasm_funcs;
} AppPlan;

extern AppPlan *app_plans;
extern int tuptables_size;

void
free_app_plans();

int32_t
env_prepare_statement(wasm_exec_env_t exec_env,
                      WASMArrayObjectRef sql_buf,
                      WASMArrayObjectRef arg_type_oids);

int32_t
env_execute_statement(wasm_exec_env_t exec_env, int32_t idx);

WASMArrayObjectRef
env_detoast(wasm_exec_env_t exec_env,
            int32_t tuptable_idx,
            int32_t row,
            int32_t col);

#endif /* RUSTICA_QUERY_H */
