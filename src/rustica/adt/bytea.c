/*
 * Copyright (c) 2025-present 燕几（北京）科技有限公司
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
#include "mb/pg_wchar.h"
#include "utils/fmgrprotos.h"

#include "wasm_runtime_common.h"

#include "rustica/datatypes.h"

static void
ensure_ascii(const char *mbstr, size_t len) {
    for (int i = 0; i < len; i++) {
        if (IS_HIGHBIT_SET(mbstr[i]))
            ereport(ERROR, errmsg("bytes_decode: input is not ASCII"));
    }
}

static wasm_externref_obj_t
bytes_decode(wasm_exec_env_t exec_env,
             wasm_obj_t refobj,
             int32_t start,
             int32_t length,
             wasm_obj_t encoding_ref) {
    // Take encoding `enc` and `is_ascii`
    char *encoding = wasm_text_copy_cstring(encoding_ref);
    int enc;
    bool is_ascii = false;
    if (pg_strcasecmp(encoding, "ascii") == 0) {
        enc = PG_UTF8;
        is_ascii = true;
    }
    else {
        enc = pg_char_to_encoding(encoding);
    }
    pfree(encoding);
    if (enc < 0)
        ereport(ERROR, errmsg("bytes_decode: invalid encoding name"));

    wasm_array_obj_t bytes_array = wasm_obj_ensure_array_i8(refobj);
    if (start < 0 || start + length > wasm_array_obj_length(bytes_array))
        ereport(ERROR, errmsg("bytes_decode: index out of bound"));
    char *bytes = ((char *)wasm_array_obj_first_elem_addr(bytes_array)) + start;
    char *str = pg_any_to_server(bytes, length, enc);

    if (str == bytes) {
        if (is_ascii)
            ensure_ascii(str, length);
        return cstring_into_varatt_obj(exec_env, str, length, TEXTOID);
    }
    else {
        length = (int)strlen(str);
        if (is_ascii)
            ensure_ascii(str, strlen(str));
        wasm_externref_obj_t rv =
            cstring_into_varatt_obj(exec_env, str, strlen(str), TEXTOID);
        pfree(str);
        return rv;
    }
}

static wasm_externref_obj_t
rst_text_encode(wasm_exec_env_t exec_env,
                wasm_obj_t refobj,
                wasm_obj_t encoding_ref) {
    // Take encoding `enc` and `is_ascii`
    char *encoding = wasm_text_copy_cstring(encoding_ref);
    int enc;
    bool is_ascii = false;
    if (pg_strcasecmp(encoding, "ascii") == 0) {
        enc = PG_UTF8;
        is_ascii = true;
    }
    else {
        enc = pg_char_to_encoding(encoding);
    }
    pfree(encoding);
    if (enc < 0)
        ereport(ERROR, errmsg("text_encode: invalid encoding name"));

    Datum jsstr = wasm_externref_obj_get_datum(refobj, TEXTOID);
    text *txt = DatumGetTextPP(jsstr);
    char *bytes = VARDATA_ANY(txt);
    int length = VARSIZE_ANY_EXHDR(txt);
    char *str = pg_server_to_any(bytes, length, enc);
    if (str != bytes)
        length = (int)strlen(str);
    if (is_ascii)
        ensure_ascii(str, length);
    wasm_externref_obj_t rv =
        cstring_into_varatt_obj(exec_env, str, length, BYTEAOID);
    if (str != bytes)
        pfree(str);
    if (jsstr != PointerGetDatum(txt))
        pfree(txt);
    return rv;
}

static wasm_externref_obj_t
rst_bytea_out(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    Datum bytea = wasm_externref_obj_get_datum(refobj, BYTEAOID);
    char *pgstr = DatumGetCString(DirectFunctionCall1(byteaout, bytea));
    wasm_externref_obj_t rv =
        cstring_into_varatt_obj(exec_env, pgstr, strlen(pgstr), TEXTOID);
    pfree(pgstr);
    return rv;
}

static NativeSymbol bytea_symbols[] = {
    { "bytes_decode", bytes_decode, "(riir)r" },
    { "text_encode", rst_text_encode, "(rr)r" },
    { "bytea_out", rst_bytea_out, "(r)r" },
};

void
rst_register_natives_bytea() {
    REGISTER_WASM_NATIVES("env", bytea_symbols);
}
