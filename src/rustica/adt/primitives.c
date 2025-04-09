/*
 * Copyright (c) 2025-present 燕几（北京）科技有限公司
 *
 * Rustica Engine is licensed under Mulan PSL v2. You can use this
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

#include "postgres.h"
#include "utils/builtins.h"

#include "wasm_runtime_common.h"
#include "rustica/datatypes.h"

static int32_t
rsl_int4in(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    char *pgstr = wasm_text_copy_cstring(refobj);
    int32_t result = pg_strtoint32_safe(pgstr, NULL);
    pfree(pgstr);
    return result;
}

static wasm_externref_obj_t
rsl_int4out(wasm_exec_env_t exec_env, int32_t int4) {
    char *pgstr =
        DatumGetCString(DirectFunctionCall1(int4out, Int32GetDatum(int4)));
    wasm_externref_obj_t rv =
        cstring_into_varatt_obj(exec_env, pgstr, strlen(pgstr), TEXTOID);
    pfree(pgstr);
    return rv;
}

static int64_t
rsl_int8in(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    char *pgstr = wasm_text_copy_cstring(refobj);
    int64_t result = pg_strtoint64_safe(pgstr, NULL);
    pfree(pgstr);
    return result;
}

static wasm_externref_obj_t
rsl_int8out(wasm_exec_env_t exec_env, int64_t int8) {
    char *pgstr =
        DatumGetCString(DirectFunctionCall1(int8out, Int64GetDatum(int8)));
    wasm_externref_obj_t rv =
        cstring_into_varatt_obj(exec_env, pgstr, strlen(pgstr), TEXTOID);
    pfree(pgstr);
    return rv;
}

static int32_t
rst_parse_bool(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    Datum jsstr = wasm_text_copy_cstring(refobj);
    text *textpp = DatumGetTextPP(jsstr);
    bool result;
    if (!parse_bool_with_len(VARDATA_ANY(textpp),
                             VARSIZE_ANY_EXHDR(textpp),
                             &result))
        ereport(ERROR, errmsg("parse_bool: invalid input"));
    if (jsstr != PointerGetDatum(textpp))
        pfree(textpp);
    return result;
}

static wasm_externref_obj_t
rst_boolout(wasm_exec_env_t exec_env, int32_t boolean) {
    char *pgstr =
        DatumGetCString(DirectFunctionCall1(boolout, BoolGetDatum(boolean)));
    wasm_externref_obj_t rv =
        cstring_into_varatt_obj(exec_env, pgstr, strlen(pgstr), TEXTOID);
    pfree(pgstr);
    return rv;
}

static float_t
rst_float4in(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    char *pgstr = wasm_text_copy_cstring(refobj);
    float_t result =
        DatumGetFloat4(DirectFunctionCall1(float4in, CStringGetDatum(pgstr)));
    pfree(pgstr);
    return result;
}

static wasm_externref_obj_t
rst_float4out(wasm_exec_env_t exec_env, float_t float4) {
    char *pgstr =
        DatumGetCString(DirectFunctionCall1(float4out, Float4GetDatum(float4)));
    wasm_externref_obj_t rv =
        cstring_into_varatt_obj(exec_env, pgstr, strlen(pgstr), TEXTOID);
    pfree(pgstr);
    return rv;
}

static double_t
rst_float8in(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    char *pgstr = wasm_text_copy_cstring(refobj);
    double_t result =
        DatumGetFloat8(DirectFunctionCall1(float8in, CStringGetDatum(pgstr)));
    pfree(pgstr);
    return result;
}

static wasm_externref_obj_t
rst_float8out(wasm_exec_env_t exec_env, double_t float8) {
    char *pgstr =
        DatumGetCString(DirectFunctionCall1(float8out, Float8GetDatum(float8)));
    wasm_externref_obj_t rv =
        cstring_into_varatt_obj(exec_env, pgstr, strlen(pgstr), TEXTOID);
    pfree(pgstr);
    return rv;
}

static NativeSymbol primitives_natives[] = {
    { "int4in", rsl_int4in, "(r)i" },
    { "int4out", rsl_int4out, "(i)r" },
    { "int8in", rsl_int8in, "(r)I" },
    { "int8out", rsl_int8out, "(I)r" },
    { "float4in", rst_float4in, "(r)f" },
    { "float4out", rst_float4out, "(f)r" },
    { "float8in", rst_float8in, "(r)F" },
    { "float8out", rst_float8out, "(F)r" },
    { "boolout", rst_boolout, "(i)r" },
    { "parse_bool", rst_parse_bool, "(r)i" },
};

void
rst_register_natives_primitives() {
    REGISTER_WASM_NATIVES("env", primitives_natives);
}
