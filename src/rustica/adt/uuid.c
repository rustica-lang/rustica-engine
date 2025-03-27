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
rsl_uuid_in(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    char *pgstr = wasm_text_copy_cstring(refobj);
    pg_uuid_t *uuid =
        DatumGetUUIDP(DirectFunctionCall1(uuid_in, CStringGetDatum(pgstr)));
    pfree(pgstr);
    return rst_externref_of_owned_datum(exec_env, UUIDPGetDatum(uuid), UUIDOID);
}

static wasm_externref_obj_t
rsl_uuid_out(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    Datum uuid = wasm_externref_obj_get_datum(refobj, UUIDOID);
    char *uuid_str = DatumGetCString(DirectFunctionCall1(uuid_out, uuid));
    wasm_externref_obj_t rv =
        cstring_into_varatt_obj(exec_env, uuid_str, strlen(uuid_str), TEXTOID);
    pfree(uuid_str);
    return rv;
}

static int32_t
rsl_uuid_cmp(wasm_exec_env_t exec_env, wasm_obj_t refobj1, wasm_obj_t refobj2) {
    Datum uuid1 = wasm_externref_obj_get_datum(refobj1, UUIDOID);
    Datum uuid2 = wasm_externref_obj_get_datum(refobj2, UUIDOID);
    return DirectFunctionCall2(uuid_cmp, uuid1, uuid2);
}

static NativeSymbol uuid_symbols[] = {
    { "uuid_in", rsl_uuid_in, "(r)r" },
    { "uuid_out", rsl_uuid_out, "(r)r" },
    { "uuid_cmp", rsl_uuid_cmp, "(rr)i" },
};

void
rst_register_natives_uuid() {
    REGISTER_WASM_NATIVES("env", uuid_symbols);
}
