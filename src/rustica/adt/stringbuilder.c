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
#include "varatt.h"
#include "catalog/pg_type_d.h"
#include "mb/pg_wchar.h"

#include "wasm_runtime_common.h"
#include "rustica/datatypes.h"
#include "rustica/module.h"

static wasm_externref_obj_t
sb_new(wasm_exec_env_t exec_env, int32_t size_hint) {
    obj_t obj =
        rst_obj_new(exec_env, OBJ_STRING_INFO, NULL, sizeof(StringInfoData));
    if (size_hint < 1)
        size_hint = 1;
    obj->flags |= OBJ_OWNS_BODY_MEMBERS;
    obj->body.sb->data = (char *)palloc(size_hint);
    obj->body.sb->maxlen = size_hint;
    resetStringInfo(obj->body.sb);
    return rst_externref_of_obj(exec_env, obj);
}

static inline StringInfo
sb_ensure_string_info(wasm_obj_t refobj) {
    obj_t obj = wasm_externref_obj_get_obj(refobj, OBJ_STRING_INFO);
    return obj->body.sb;
}

static int32_t
sb_mblength(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    StringInfo sb = sb_ensure_string_info(refobj);
    return sb->len;
}

static int32_t
sb_write_string(wasm_exec_env_t exec_env, wasm_obj_t refobj, wasm_obj_t str) {
    StringInfo sb = sb_ensure_string_info(refobj);
    Datum jsstr = wasm_externref_obj_get_datum(str, TEXTOID);
    text *txt = DatumGetTextPP(jsstr);
    appendBinaryStringInfoNT(sb, VARDATA_ANY(txt), VARSIZE_ANY_EXHDR(txt));
    if (jsstr != PointerGetDatum(txt))
        pfree(txt);
    return 0;
}

static int32_t
sb_write_substring(wasm_exec_env_t exec_env,
                   wasm_obj_t refobj,
                   wasm_obj_t str,
                   int32_t start,
                   int32_t len) {
    StringInfo sb = sb_ensure_string_info(refobj);
    Datum jsstr = wasm_externref_obj_get_datum(str, TEXTOID);
    text *txt = DatumGetTextPP(jsstr);
    char *data = VARDATA_ANY(txt);
    int size = VARSIZE_ANY_EXHDR(txt);
    int mb_start = pg_mbcharcliplen(data, size, start);
    char *clip = data + mb_start;
    appendBinaryStringInfoNT(sb,
                             clip,
                             pg_mbcharcliplen(clip, size - mb_start, len));
    if (jsstr != PointerGetDatum(txt))
        pfree(txt);
    return 0;
}

static int32_t
sb_write_char(wasm_exec_env_t exec_env, wasm_obj_t refobj, int32_t ch) {
    StringInfo sb = sb_ensure_string_info(refobj);
    enlargeStringInfo(sb, MAX_UNICODE_EQUIVALENT_STRING);
    pg_unicode_to_server(ch, (unsigned char *)sb->data + sb->len);
    sb->len += (int)strlen(sb->data + sb->len);
    return 0;
}

static int32_t
sb_write_bytes(wasm_exec_env_t exec_env,
               wasm_obj_t refobj,
               wasm_obj_t bytes,
               int32_t start,
               int32_t len) {
    StringInfo sb = sb_ensure_string_info(refobj);
    bytea *b = DatumGetByteaP(wasm_externref_obj_get_datum(bytes, BYTEAOID));
    if (start < 0 || start + len > VARSIZE_ANY_EXHDR(b))
        ereport(ERROR, errmsg("sb_write_bytes: index out of bound"));
    appendBinaryStringInfoNT(sb, (char *)VARDATA_ANY(b) + start, len);
    return 0;
}

static int32_t
sb_write_byte(wasm_exec_env_t exec_env, wasm_obj_t refobj, int32_t byte) {
    StringInfo sb = sb_ensure_string_info(refobj);
    enlargeStringInfo(sb, 1);
    sb->data[sb->len++] = (char)byte;
    return 0;
}

static wasm_externref_obj_t
sb_to_string(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    StringInfo sb = sb_ensure_string_info(refobj);
    return cstring_into_varatt_obj(exec_env, sb->data, sb->len, TEXTOID);
}

static wasm_externref_obj_t
sb_to_bytes(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    StringInfo sb = sb_ensure_string_info(refobj);
    return cstring_into_varatt_obj(exec_env, sb->data, sb->len, BYTEAOID);
}

static NativeSymbol sb_symbols[] = {
    { "sb_new", sb_new, "(i)r" },
    { "sb_mblength", sb_mblength, "(r)i" },
    { "sb_write_string", sb_write_string, "(rr)i" },
    { "sb_write_substring", sb_write_substring, "(rrii)i" },
    { "sb_write_char", sb_write_char, "(ri)i" },
    { "sb_write_bytes", sb_write_bytes, "(rrii)i" },
    { "sb_write_byte", sb_write_byte, "(ri)i" },
    { "sb_to_string", sb_to_string, "(r)r" },
    { "sb_to_bytes", sb_to_bytes, "(r)r" },
};

void
rst_register_natives_stringbuilder() {
    REGISTER_WASM_NATIVES("env", sb_symbols);
}
