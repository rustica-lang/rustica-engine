/*
 * Copyright (c) 2025-present 燕几（北京）科技有限公司
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
#include "catalog/pg_type_d.h"
#include "utils/builtins.h"
#include "utils/uuid.h"

#include "wasm_runtime_common.h"
#include "rustica/datatypes.h"

static wasm_externref_obj_t
rsl_json_in(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    Datum str = wasm_externref_obj_get_datum(refobj, TEXTOID);
    obj_t obj = rst_obj_new(
        exec_env,
        OBJ_DATUM,
        wasm_externref_obj_to_internal_obj((wasm_externref_obj_t)refobj),
        0);
    obj->oid = JSONOID;
    obj->body.datum = str;
    return rst_externref_of_obj(exec_env, obj);
}

static wasm_externref_obj_t
rsl_json_out(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    Datum json = wasm_externref_obj_get_datum(refobj, JSONOID);
    obj_t obj = rst_obj_new(
        exec_env,
        OBJ_DATUM,
        wasm_externref_obj_to_internal_obj((wasm_externref_obj_t)refobj),
        0);
    obj->oid = TEXTOID;
    obj->body.datum = json;
    return rst_externref_of_obj(exec_env, obj);
}

static NativeSymbol json_symbols[] = {
    { "json_in", rsl_json_in, "(r)r" },
    { "json_out", rsl_json_out, "(r)r" },
};

void
rst_register_natives_json() {
    REGISTER_WASM_NATIVES("env", json_symbols);
}
