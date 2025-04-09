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

    bytea *bytes_datum = (bytea *)DatumGetPointer(
        wasm_externref_obj_get_datum(refobj, BYTEAOID));
    if (start < 0 || start + length > VARSIZE_ANY_EXHDR(bytes_datum))
        ereport(ERROR, errmsg("bytes_decode: index out of bound"));
    char *bytes = ((char *)VARDATA_ANY(bytes_datum)) + start;
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

static wasm_externref_obj_t
rst_bytea_repeat(wasm_exec_env_t exec_env, int32_t byte, int32_t count) {
    obj_t obj;
    if (count <= VARATT_SHORT_MAX - VARHDRSZ_SHORT) {
        obj = rst_obj_new(exec_env, OBJ_DATUM, NULL, count + VARHDRSZ_SHORT);
        SET_VARSIZE_1B(DatumGetPointer(obj->body.datum),
                       count + VARHDRSZ_SHORT);
    }
    else if (count <= VARATT_MAX - VARHDRSZ) {
        obj = rst_obj_new(exec_env, OBJ_DATUM, NULL, count + VARHDRSZ);
        SET_VARSIZE(DatumGetPointer(obj->body.datum), count + VARHDRSZ);
    }
    else {
        ereport(ERROR, errmsg("bytea_repeat: count too large"));
    }
    obj->oid = BYTEAOID;
    memset(VARDATA_ANY(DatumGetPointer(obj->body.datum)), byte, count);
    return rst_externref_of_obj(exec_env, obj);
}

static int32_t
rst_byteaeq(wasm_exec_env_t exec_env, wasm_obj_t refobj1, wasm_obj_t refobj2) {
    Datum bytea1 = wasm_externref_obj_get_datum(refobj1, BYTEAOID);
    Datum bytea2 = wasm_externref_obj_get_datum(refobj2, BYTEAOID);
    return DatumGetBool(DirectFunctionCall2(byteaeq, bytea1, bytea2));
}

static int32_t
rst_byteaoctetlen(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    Datum bytea = wasm_externref_obj_get_datum(refobj, BYTEAOID);
    return DatumGetInt32(DirectFunctionCall1(byteaoctetlen, bytea));
}

static wasm_externref_obj_t
rst_bytea_substr(wasm_exec_env_t exec_env,
                 wasm_obj_t refobj,
                 int32_t start,
                 int32_t length) {
    Datum bytea = wasm_externref_obj_get_datum(refobj, BYTEAOID);
    Datum rv = DirectFunctionCall3(bytea_substr,
                                   bytea,
                                   Int32GetDatum(start),
                                   Int32GetDatum(length));
    return rst_externref_of_owned_datum(exec_env, rv, BYTEAOID);
}

static WASMValue *
global_bytes_resolver(const char *utf8str,
                      WASMRefType *ref_type,
                      uint8 val_type,
                      uint8 is_mutable) {
    size_t llen = strlen(utf8str);
    if (llen > VARATT_MAX - VARHDRSZ)
        ereport(ERROR, errmsg("global bytes too long"));
    int len = (int)llen;
    obj_t obj;
    WASMValue *rv;
    if (len <= VARATT_SHORT_MAX - VARHDRSZ_SHORT) {
        rv = rst_obj_new_static(OBJ_DATUM, &obj, len + VARHDRSZ_SHORT);
        SET_VARSIZE_1B(DatumGetPointer(obj->body.datum), len + VARHDRSZ_SHORT);
    }
    else {
        rv = rst_obj_new_static(OBJ_DATUM, &obj, len + VARHDRSZ);
        SET_VARSIZE(DatumGetPointer(obj->body.datum), len + VARHDRSZ);
    }
    obj->oid = BYTEAOID;
    memcpy(VARDATA_ANY(DatumGetPointer(obj->body.datum)), utf8str, len);
    return rv;
}

static NativeSymbol bytea_symbols[] = {
    { "bytes_decode", bytes_decode, "(riir)r" },
    { "text_encode", rst_text_encode, "(rr)r" },
    { "bytea_out", rst_bytea_out, "(r)r" },
    { "bytea_repeat", rst_bytea_repeat, "(ii)r" },
    { "byteaeq", rst_byteaeq, "(rr)i" },
    { "byteaoctetlen", rst_byteaoctetlen, "(r)i" },
    { "bytea_substr", rst_bytea_substr, "(rii)r" },
};

void
rst_register_natives_bytea() {
    REGISTER_WASM_NATIVES("env", bytea_symbols);
    if (!wasm_register_global_resolver("env:bytea", global_bytes_resolver))
        ereport(ERROR, errmsg("cannot register global resolver for bytea"));
}
