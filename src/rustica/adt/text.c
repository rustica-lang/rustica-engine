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
#include "fmgr.h"
#include "varatt.h"
#include "access/detoast.h"
#include "catalog/pg_collation_d.h"
#include "catalog/pg_type_d.h"
#include "mb/pg_wchar.h"
#include "utils/fmgrprotos.h"

#include "wasm_runtime_common.h"
#include "rustica/datatypes.h"

static int32_t
rst_textlen(wasm_exec_env_t exec_env, wasm_obj_t ref) {
    Datum str = wasm_externref_obj_get_datum(ref, TEXTOID);
    // This is pretty much textlen() with an early free of the detoasted text
    if (pg_database_encoding_max_length() == 1) {
        return (int32_t)(toast_raw_datum_size(str) - VARHDRSZ);
    }
    else {
        text *t = DatumGetTextPP(str);
        int rv = pg_mbstrlen_with_len(VARDATA_ANY(t), VARSIZE_ANY_EXHDR(t));
        RST_FREE_IF_COPY(t, str);
        return rv;
    }
}

static int32_t
rst_textget(wasm_exec_env_t exec_env, wasm_obj_t ref, int32_t index) {
    Datum str = wasm_externref_obj_get_datum(ref, TEXTOID);
    text *t = DatumGetTextPP(str);
    char *data = VARDATA_ANY(t);
    int size = VARSIZE_ANY_EXHDR(t);
    pg_wchar rv[2];

    int offset = 0;
    if (pg_database_encoding_max_length() == 1) {
        offset = index;
        if (offset < 0)
            offset += size;
        if (offset < 0 || offset >= size)
            goto error;
    }
    else if (index != 0) {
        if (index < 0) {
            index += pg_mbstrlen_with_len(data, size);
            if (index < 0)
                goto error;
        }
        offset = pg_mbcharcliplen(data, size, index);
        if (offset >= size)
            goto error;
    }

    pg_mb2wchar_with_len(data + offset, rv, 1);
    RST_FREE_IF_COPY(t, str);
    return (int32_t)rv[0];

error:
    RST_FREE_IF_COPY(t, str);
    ereport(ERROR,
            (errcode(ERRCODE_SUBSTRING_ERROR), errmsg("index out of range")));
}

static int32_t
rst_texteq(wasm_exec_env_t exec_env, wasm_obj_t x, wasm_obj_t y) {
    Datum t1 = wasm_externref_obj_get_datum(x, TEXTOID);
    Datum t2 = wasm_externref_obj_get_datum(y, TEXTOID);
    Datum result =
        DirectFunctionCall2Coll(texteq, DEFAULT_COLLATION_OID, t1, t2);
    return DatumGetBool(result);
}

static wasm_externref_obj_t
rst_textcat(wasm_exec_env_t exec_env, wasm_obj_t x, wasm_obj_t y) {
    Datum t1 = wasm_externref_obj_get_datum(x, TEXTOID);
    Datum t2 = wasm_externref_obj_get_datum(y, TEXTOID);
    Datum result = DirectFunctionCall2(textcat, t1, t2);
    return rst_externref_of_owned_datum(exec_env, result, TEXTOID);
}

static wasm_externref_obj_t
rst_text_substr(wasm_exec_env_t exec_env,
                wasm_obj_t obj,
                int32_t start,
                int32_t length) {
    Datum str = wasm_externref_obj_get_datum(obj, TEXTOID);
    Datum rv;
    if (length == -1)
        rv = DirectFunctionCall2(text_substr_no_len, str, Int32GetDatum(start));
    else
        rv = DirectFunctionCall3(text_substr,
                                 str,
                                 Int32GetDatum(start),
                                 Int32GetDatum(length));
    return rst_externref_of_owned_datum(exec_env, rv, TEXTOID);
}

static WASMValue *
global_text_resolver(const char *utf8str,
                     WASMRefType *ref_type,
                     uint8 val_type,
                     uint8 is_mutable) {
    // Convert the WASM string from UTF-8 to server encoding
    size_t llen = strlen(utf8str);
    if (llen > VARATT_MAX - VARHDRSZ)
        ereport(ERROR, errmsg("global text too long"));
    int len = (int)llen;
    char *str = pg_any_to_server(utf8str, len, PG_UTF8);
    if (str != utf8str) {
        llen = strlen(str);
        if (llen > VARATT_MAX - VARHDRSZ) {
            pfree(str);
            ereport(ERROR,
                    errmsg("global text too long after encoding conversion"));
        }
        len = (int)llen;
    }
    bool is_short = len <= VARATT_SHORT_MAX - VARHDRSZ_SHORT;

    // Create a static object with the Datum embedded
    obj_t obj;
    WASMValue *rv =
        rst_obj_new_static(OBJ_DATUM,
                           &obj,
                           (is_short ? VARHDRSZ_SHORT : VARHDRSZ) + len);
    obj->oid = TEXTOID;
    text *t = (text *)DatumGetPointer(obj->body.datum);

    // Copy the converted string into the text Datum
    if (is_short)
        SET_VARSIZE_1B(t, len + VARHDRSZ_SHORT);
    else
        SET_VARSIZE(t, len + VARHDRSZ);
    memcpy(VARDATA_ANY(t), str, len);

    // Free the converted string if necessary
    if (str != utf8str)
        pfree(str);
    return rv;
}

static NativeSymbol text_natives[] = {
    { "textlen", rst_textlen, "(r)i" },
    { "textget", rst_textget, "(ri)i" },
    { "texteq", rst_texteq, "(rr)i" },
    { "textcat", rst_textcat, "(rr)r" },
    { "text_substr", rst_text_substr, "(rii)r" },
};

void
rst_register_natives_text() {
    REGISTER_WASM_NATIVES("env", text_natives);
    if (!wasm_register_global_resolver("env:text", global_text_resolver))
        ereport(ERROR, errmsg("cannot register global resolver for texts"));
}
