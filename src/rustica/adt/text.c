// SPDX-FileCopyrightText: 2025 燕几（北京）科技有限公司
// SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0

#include "postgres.h"
#include "fmgr.h"
#include "varatt.h"
#include "access/detoast.h"
#include "catalog/pg_collation_d.h"
#include "catalog/pg_type_d.h"
#include "mb/pg_wchar.h"
#include "utils/builtins.h"
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
                     uint32_t utf8len,
                     WASMRefType *ref_type,
                     uint8 val_type,
                     uint8 is_mutable) {
    // Convert the WASM string from UTF-8 to server encoding
    size_t llen = utf8len;
    if (llen > VARATT_MAX - VARHDRSZ)
        ereport(ERROR, errmsg("global text too long"));
    int len = (int)llen;
    if (GetDatabaseEncoding() != PG_UTF8) {
        ereport(ERROR, errmsg("TODO"));
    }
    char *str = utf8str;
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

static int32_t
utf16_count_code_units(const char *mbstr, int size) {
    int count = 0;
    int mb_offset = 0;
    while (mb_offset < size) {
        int ch = pg_mblen(mbstr + mb_offset);
        if (ch <= 0)
            ch = 1; // Invalid UTF-8 sequence, treat as single byte
        else if (mb_offset + ch > size)
            break; // Incomplete character at end of string
        if (ch < 4)
            count += 1;  // Normal character
        else
            count += 2; // Surrogate pair
        mb_offset += ch;
    }
    return count;
}

static int32_t
utf16_length(wasm_exec_env_t exec_env, wasm_obj_t obj) {
    Datum str = wasm_externref_obj_get_datum(obj, TEXTOID);
    text *t = DatumGetTextPP(str);
    char *data = VARDATA_ANY(t);
    int size = VARSIZE_ANY_EXHDR(t);
    int count;
    if (pg_database_encoding_max_length() == 1) {
        count = size;
    } else {
        count = utf16_count_code_units(data, size);
    }
    RST_FREE_IF_COPY(t, str);
    return count;
}

static int32_t
utf16_char_code_at(wasm_exec_env_t exec_env, wasm_obj_t ref, int32_t index) {
    Datum str = wasm_externref_obj_get_datum(ref, TEXTOID);
    text *t = DatumGetTextPP(str);
    char *data = VARDATA_ANY(t);
    int size = VARSIZE_ANY_EXHDR(t);
    pg_wchar rv[2] = { '?', 0 };

    if (pg_database_encoding_max_length() == 1) {
        if (index < 0)
            index += size;
        if (index < 0 || index >= size)
            goto oob_error;
        rv[0] = (unsigned char)data[index];
    } else {
        int mb_offset = 0;
        int utf16_index = 0;
        int32_t utf16_size = utf16_count_code_units(data, size);
        if (index < 0)
            index += utf16_size;
        if (index < 0 || index >= utf16_size)
            goto oob_error;
        // We're safe within utf16_size not to check for out-of-bounds
        for (;;) {
            int ch = pg_mblen(data + mb_offset);
            if (ch <= 0)
                ch = 1; // Invalid UTF-8 sequence, treat as single byte
            if (ch < 4)
                utf16_index += 1;
            else
                utf16_index += 2;
            if (utf16_index > index) {
                // The character at the given UTF-16 index starts here
                pg_mb2wchar_with_len(data + mb_offset, rv, ch);
                if (ch >= 4) {
                    if (utf16_index - 1 == index)
                        // We are in the second half of a surrogate pair
                        // Return the low surrogate code unit
                        rv[0] = 0xDC00 + (rv[0] & 0x3FF);
                    else
                        // We are in the first half of a surrogate pair
                        // Return the high surrogate code unit
                        rv[0] = 0xD800 + ((rv[0] - 0x10000) >> 10);
                }
                break;
            }
            mb_offset += ch;
        }
    }
    RST_FREE_IF_COPY(t, str);
    return (int32_t)rv[0];

oob_error:
    RST_FREE_IF_COPY(t, str);
    ereport(ERROR,
            (errcode(ERRCODE_SUBSTRING_ERROR), errmsg("index out of range")));
}

static wasm_externref_obj_t
utf16_from_char_code_array(wasm_exec_env_t exec_env, wasm_obj_t obj, int32_t start, int32_t length) {
    wasm_array_obj_t arr = (wasm_array_obj_t)obj;
    uint32_t size = wasm_array_obj_length(arr);
    StringInfoData buf;
    wasm_externref_obj_t rv;
    const char* err = "fromCharCodeArray failed";

    if (length < 0)
        length = size; // To the end
    if (start < 0)
        start += size;
    if (start < 0 || start > size)
        ereport(ERROR,
                (errcode(ERRCODE_SUBSTRING_ERROR), errmsg("start index out of range")));
    if (start + length > size)
        length = size - start; // Adjust to fit
    if (length == 0)
        return rst_externref_of_owned_datum(exec_env, CStringGetTextDatum(""), TEXTOID);
    initStringInfo(&buf);
    for (uint32_t i = start; i < size; i++) {
        wasm_value_t value;
        int32_t cu;
        pg_wchar ch;
        unsigned char utf8[4];

        wasm_array_obj_get_elem(arr, i, false, &value);
        cu = value.i32;
        if (cu < 0 || cu > 0xFFFF) {
            err = "Invalid UTF-16 code unit";
            goto error;
        }
        if (is_utf16_surrogate_first(cu)) {
            // High surrogate, must be followed by a low surrogate
            if (i + 1 < size) {
                wasm_array_obj_get_elem(arr, i + 1, false, &value);
                if (is_utf16_surrogate_second(value.i32)) {
                    ch = surrogate_pair_to_codepoint(cu, value.i32);
                    i++; // Consumed the low surrogate
                } else {
                    err = "Invalid surrogate pair";
                    goto error;
                }
            } else {
                err = "Incomplete surrogate pair";
                goto error;
            }
        } else if (is_utf16_surrogate_second(cu)) {
            err = "Unmatched low surrogate";
            goto error;
        } else {
            ch = cu; // Normal code unit
        }
        unicode_to_utf8(ch, utf8);
        appendBinaryStringInfo(&buf, (char *)utf8, unicode_utf8len(ch));
    }
    rv = cstring_into_varatt_obj(exec_env, buf.data, buf.len, TEXTOID);
    pfree(buf.data);
    return rv;

error:
    pfree(buf.data);
    wasm_runtime_set_exception(wasm_runtime_get_module_inst(exec_env), err);
    return NULL;
}

static wasm_externref_obj_t
utf16_from_code_point(wasm_exec_env_t exec_env, int32_t code_point) {
    unsigned char buf[5];
    if (code_point == 0) {
        obj_t obj = rst_obj_new(exec_env, OBJ_DATUM, NULL, VARHDRSZ_SHORT  + 1);
        obj->oid = TEXTOID;
        SET_VARSIZE_1B(obj->body.ptr, VARHDRSZ_SHORT + 1);
        VARDATA_ANY(obj->body.ptr)[0] = '\0';
        return rst_externref_of_obj(exec_env, obj);
    }
    pg_unicode_to_server(code_point, buf);
    return cstring_into_varatt_obj(exec_env, buf, strlen(buf), TEXTOID);
}

static void
console_log(wasm_exec_env_t exec_env, wasm_obj_t obj) {
    Datum str = wasm_externref_obj_get_datum(obj, TEXTOID);
    text *t = DatumGetTextPP(str);
    char *cstr = text_to_cstring(t);
    RST_FREE_IF_COPY(t, str);
    printf("%s\n", cstr);
    pfree(cstr);
}

static NativeSymbol text_natives[] = {
    { "textlen", rst_textlen, "(r)i" },
    { "textget", rst_textget, "(ri)i" },
    { "texteq", rst_texteq, "(rr)i" },
    { "textcat", rst_textcat, "(rr)r" },
    { "text_substr", rst_text_substr, "(rii)r" },
};

static NativeSymbol wasm_js_string_natives[] = {
    { "length", utf16_length, "(r)i" },
    { "charCodeAt", utf16_char_code_at, "(ri)i" },
    { "fromCharCodeArray", utf16_from_char_code_array, "(rii)r" },
    { "equals", rst_texteq, "(rr)i" },
    { "concat", rst_textcat, "(rr)r" },
    { "fromCodePoint", utf16_from_code_point, "(i)r" },
};

static NativeSymbol console_natives[] = {
    { "log", console_log, "(r)" },
};

void
rst_register_natives_text() {
    REGISTER_WASM_NATIVES("env", text_natives);
    REGISTER_WASM_NATIVES("wasm:js-string", wasm_js_string_natives);
    REGISTER_WASM_NATIVES("console", console_natives);
    if (!wasm_register_global_resolver("env:text", global_text_resolver))
        ereport(ERROR, errmsg("cannot register global resolver for texts"));
}
