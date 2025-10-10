// SPDX-FileCopyrightText: 2025 燕几（北京）科技有限公司
// SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0

#include "postgres.h"
#include "common/jsonapi.h"
#include "mb/pg_wchar.h"
#include "utils/builtins.h"

#include "wasm_runtime_common.h"
#include "rustica/datatypes.h"
#include "rustica/query.h"
#include "rustica/wamr.h"

static wasm_externref_obj_t
rsl_jsonb_in(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    char *pgstr = wasm_text_copy_cstring(refobj);
    Datum rv = DirectFunctionCall1(jsonb_in, CStringGetDatum(pgstr));
    pfree(pgstr);
    return rst_externref_of_owned_datum(exec_env, rv, JSONBOID);
}

static wasm_externref_obj_t
rsl_jsonb_out(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    Datum jsonb = wasm_externref_obj_get_datum(refobj, JSONBOID);
    char *pgstr = DatumGetCString(DirectFunctionCall1(jsonb_out, jsonb));
    wasm_externref_obj_t rv =
        cstring_into_varatt_obj(exec_env, pgstr, strlen(pgstr), TEXTOID);
    pfree(pgstr);
    return rv;
}

static wasm_externref_obj_t
jsonb_value_to_jsonb(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    obj_t obj = wasm_externref_obj_get_obj(refobj, OBJ_JSONB_VALUE);
    Datum rv = JsonbPGetDatum(JsonbValueToJsonb(obj->body.jbv));
    return rst_externref_of_owned_datum(exec_env, rv, JSONBOID);
}

static wasm_externref_obj_t
jsonb_value_obj_new(wasm_exec_env_t exec_env,
                    JsonbValue *jbv,
                    wasm_obj_t ref,
                    bool owns_body_members) {
    obj_t obj = rst_obj_new(exec_env, OBJ_JSONB_VALUE, ref, sizeof(JsonbValue));
    *obj->body.jbv = *jbv;
    if (owns_body_members)
        obj->flags |= OBJ_OWNS_BODY_MEMBERS;
    return rst_externref_of_obj(exec_env, obj);
}

static wasm_externref_obj_t
jsonb_value_new_null(wasm_exec_env_t exec_env) {
    JsonbValue jbv = { .type = jbvNull };
    return jsonb_value_obj_new(exec_env, &jbv, NULL, false);
}

static wasm_externref_obj_t
jsonb_value_new_string(wasm_exec_env_t exec_env, wasm_obj_t str) {
    Datum jsstr = wasm_externref_obj_get_datum(str, TEXTOID);
    text *txt = DatumGetTextPP(jsstr);
    wasm_obj_t ref = NULL;
    JsonbValue jbv = { .type = jbvString };
    bool owns_body_members = false;
    if (jsstr == PointerGetDatum(txt)) {
        ref = wasm_externref_obj_to_internal_obj((wasm_externref_obj_t)str);
        jbv.val.string.len = VARSIZE_ANY_EXHDR(txt);
        jbv.val.string.val = VARDATA_ANY(txt);
    }
    else {
        char *pgstr = text_to_cstring(txt);
        pfree(txt);
        owns_body_members = true;
        jbv.val.string.len = (int)strlen(pgstr);
        jbv.val.string.val = pgstr;
    }
    return jsonb_value_obj_new(exec_env, &jbv, ref, owns_body_members);
}

static wasm_externref_obj_t
jsonb_value_new_numeric(wasm_exec_env_t exec_env, double_t val) {
    JsonbValue jbv = { .type = jbvNumeric,
                       .val.numeric = DatumGetNumeric(
                           DirectFunctionCall1(float8_numeric,
                                               Float8GetDatum(val))) };
    return jsonb_value_obj_new(exec_env, &jbv, NULL, false);
}

static wasm_externref_obj_t
jsonb_value_new_bool(wasm_exec_env_t exec_env, int32_t val) {
    JsonbValue jbv = { .type = jbvBool, .val.boolean = val };
    return jsonb_value_obj_new(exec_env, &jbv, NULL, false);
}

static wasm_externref_obj_t
jsonb_value_new_array(wasm_exec_env_t exec_env, int32_t length) {
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    wasm_array_obj_t ref_set =
        wasm_array_obj_new(exec_env, ctx->anyref_array, length, NULL);
    JsonbValue jbv = { .type = jbvArray,
                       .val.array.nElems = length,
                       .val.array.elems =
                           (JsonbValue *)palloc(sizeof(JsonbValue) * length),
                       .val.array.rawScalar = false };
    return jsonb_value_obj_new(exec_env, &jbv, (wasm_obj_t)ref_set, true);
}

static int32_t
jsonb_value_array_set(wasm_exec_env_t exec_env,
                      wasm_obj_t array,
                      int32_t idx,
                      wasm_obj_t member) {
    obj_t obj = wasm_externref_obj_get_obj(array, OBJ_JSONB_VALUE);
    obj_t member_obj = wasm_externref_obj_get_obj(member, OBJ_JSONB_VALUE);
    if (!(obj->flags & OBJ_REFERENCING))
        ereport(ERROR,
                errmsg("jsonb_value_array_set: array is not referencing"));
    WASMValue val = { .gc_obj = member };
    wasm_array_obj_set_elem((wasm_array_obj_t)obj->ref->val, idx, &val);
    obj->body.jbv->val.array.elems[idx] = *member_obj->body.jbv;
    return 0;
}

static wasm_externref_obj_t
jsonb_value_new_object(wasm_exec_env_t exec_env, int32_t length) {
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    wasm_array_obj_t ref_set =
        wasm_array_obj_new(exec_env, ctx->anyref_array, length * 2, NULL);

    JsonbValue jbv = { .type = jbvObject,
                       .val.object.nPairs = length,
                       .val.object.pairs =
                           (JsonbPair *)palloc(sizeof(JsonbPair) * length) };
    return jsonb_value_obj_new(exec_env, &jbv, (wasm_obj_t)ref_set, true);
}

static int32_t
jsonb_value_object_set(wasm_exec_env_t exec_env,
                       wasm_obj_t object,
                       int32_t idx,
                       wasm_obj_t key,
                       wasm_obj_t value) {
    obj_t obj = wasm_externref_obj_get_obj(object, OBJ_JSONB_VALUE);
    obj_t key_obj = wasm_externref_obj_get_obj(key, OBJ_JSONB_VALUE);
    obj_t value_obj = wasm_externref_obj_get_obj(value, OBJ_JSONB_VALUE);
    if (!(obj->flags & OBJ_REFERENCING))
        ereport(ERROR,
                errmsg("jsonb_value_object_set: object is not referencing"));
    if (key_obj->body.jbv->type != jbvString)
        ereport(ERROR, errmsg("jsonb_value_object_set: key is not a string"));
    WASMValue val = { .gc_obj = key };
    wasm_array_obj_set_elem((wasm_array_obj_t)obj->ref->val, idx * 2, &val);
    obj->body.jbv->val.object.pairs[idx].key = *key_obj->body.jbv;
    val.gc_obj = value;
    wasm_array_obj_set_elem((wasm_array_obj_t)obj->ref->val, idx * 2 + 1, &val);
    obj->body.jbv->val.object.pairs[idx].value = *value_obj->body.jbv;
    return 0;
}

static JsonParseErrorType
object_start(void *pstate) {
    wasm_exec_env_t exec_env = (wasm_exec_env_t)pstate;
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    uint32 argv_buf[1] = { 0 };
    wasm_function_inst_t func = ctx->json_parse_object_start;
    if (!func)
        return JSON_SEM_ACTION_FAILED;
    bool rv = wasm_runtime_call_wasm(exec_env, func, 0, argv_buf);
    return rv ? JSON_SUCCESS : JSON_SEM_ACTION_FAILED;
}

static JsonParseErrorType
object_end(void *pstate) {
    wasm_exec_env_t exec_env = (wasm_exec_env_t)pstate;
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    uint32 argv_buf[1] = { 0 };
    wasm_function_inst_t func = ctx->json_parse_object_end;
    if (!func)
        return JSON_SEM_ACTION_FAILED;
    bool rv = wasm_runtime_call_wasm(exec_env, func, 0, argv_buf);
    return rv ? JSON_SUCCESS : JSON_SEM_ACTION_FAILED;
}

static JsonParseErrorType
array_start(void *pstate) {
    wasm_exec_env_t exec_env = (wasm_exec_env_t)pstate;
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    uint32 argv_buf[1] = { 0 };
    wasm_function_inst_t func = ctx->json_parse_array_start;
    if (!func)
        return JSON_SEM_ACTION_FAILED;
    bool rv = wasm_runtime_call_wasm(exec_env, func, 0, argv_buf);
    return rv ? JSON_SUCCESS : JSON_SEM_ACTION_FAILED;
}

static JsonParseErrorType
array_end(void *pstate) {
    wasm_exec_env_t exec_env = (wasm_exec_env_t)pstate;
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    uint32 argv_buf[1] = { 0 };
    wasm_function_inst_t func = ctx->json_parse_array_end;
    if (!func)
        return JSON_SEM_ACTION_FAILED;
    bool rv = wasm_runtime_call_wasm(exec_env, func, 0, argv_buf);
    return rv ? JSON_SUCCESS : JSON_SEM_ACTION_FAILED;
}

static JsonParseErrorType
object_field_start(void *pstate, char *fname, bool isnull) {
    wasm_exec_env_t exec_env = (wasm_exec_env_t)pstate;
    wasm_externref_obj_t key =
        cstring_into_varatt_obj(exec_env, fname, strlen(fname), TEXTOID);
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    wasm_val_t arg = { .kind = WASM_EXTERNREF, .of.foreign = key };
    wasm_function_inst_t func = ctx->json_parse_object_field_start;
    if (!func)
        return JSON_SEM_ACTION_FAILED;
    bool rv = wasm_runtime_call_wasm_a(exec_env, func, 0, NULL, 1, &arg);
    return rv ? JSON_SUCCESS : JSON_SEM_ACTION_FAILED;
}

static JsonParseErrorType
push_scalar(void *pstate, char *token, JsonTokenType tokentype) {
    wasm_exec_env_t exec_env = (wasm_exec_env_t)pstate;
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    wasm_val_t arg = { 0 };
    wasm_function_inst_t func = NULL;
    switch (tokentype) {
        case JSON_TOKEN_STRING:
            arg.kind = WASM_EXTERNREF;
            arg.of.foreign = cstring_into_varatt_obj(exec_env,
                                                     token,
                                                     strlen(token),
                                                     TEXTOID);
            func = ctx->json_parse_push_string;
            break;

        case JSON_TOKEN_NUMBER:
        {
            Datum num;
            // TODO: replace with numeric_in as PG does
            DirectInputFunctionCallSafe(float8in,
                                        token,
                                        InvalidOid,
                                        -1,
                                        NULL,
                                        &num);
            arg.kind = WASM_F64;
            arg.of.f64 = DatumGetFloat8(num);
            func = ctx->json_parse_push_number;
            break;
        }

        case JSON_TOKEN_TRUE:
        case JSON_TOKEN_FALSE:
            arg.kind = WASM_I32;
            arg.of.i32 = tokentype == JSON_TOKEN_TRUE;
            func = ctx->json_parse_push_bool;
            break;

        case JSON_TOKEN_NULL:
        {
            func = ctx->json_parse_push_null;
            if (!func)
                return JSON_SEM_ACTION_FAILED;
            bool rv = wasm_runtime_call_wasm(exec_env, func, 0, NULL);
            return rv ? JSON_SUCCESS : JSON_SEM_ACTION_FAILED;
        }

        default:
            ereport(ERROR, errmsg("push_scalar: invalid token type"));
    }

    if (!func)
        return JSON_SEM_ACTION_FAILED;
    bool rv = wasm_runtime_call_wasm_a(exec_env, func, 0, NULL, 1, &arg);
    return rv ? JSON_SUCCESS : JSON_SEM_ACTION_FAILED;
}

static int32_t
rst_json_parse(wasm_exec_env_t exec_env,
               wasm_obj_t buf,
               int32_t start,
               int32_t length) {
    Datum buf_datum = wasm_externref_obj_get_datum(buf, BYTEAOID);
    char *ptr = VARDATA_ANY(DatumGetPointer(buf_datum));
    JsonLexContext *lex =
        makeJsonLexContextCstringLen(ptr + start, length, PG_UTF8, true);
    JsonSemAction sem = {
        .semstate = exec_env,
        .object_start = object_start,
        .object_end = object_end,
        .array_start = array_start,
        .array_end = array_end,
        .object_field_start = object_field_start,
        .scalar = push_scalar,
    };
    return pg_parse_json(lex, &sem) == JSON_SUCCESS;
}

static int32_t
rst_jsonb_iterate(wasm_exec_env_t exec_env, wasm_obj_t jsonb) {
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);
    Jsonb *jb = DatumGetJsonbP(wasm_externref_obj_get_datum(jsonb, JSONBOID));
    JsonbIterator *it = JsonbIteratorInit(&jb->root);
    wasm_function_inst_t func;
    JsonbValue jbv;
    bool raw_scalar = false;
    bool success = true;
    while (true) {
        switch (JsonbIteratorNext(&it, &jbv, false)) {
            case WJB_BEGIN_ARRAY:
                Assert(!raw_scalar);
                if (jbv.val.array.rawScalar)
                    raw_scalar = true;
                else if (success) {
                    if ((func = ctx->json_parse_array_start))
                        if (!wasm_runtime_call_wasm(exec_env, func, 0, NULL))
                            success = false;
                }
                break;

            case WJB_END_ARRAY:
                if (!raw_scalar && success)
                    if ((func = ctx->json_parse_array_end))
                        if (!wasm_runtime_call_wasm(exec_env, func, 0, NULL))
                            success = false;
                break;

            case WJB_BEGIN_OBJECT:
                if (success)
                    if ((func = ctx->json_parse_object_start))
                        if (!wasm_runtime_call_wasm(exec_env, func, 0, NULL))
                            success = false;
                break;

            case WJB_END_OBJECT:
                if (success)
                    if ((func = ctx->json_parse_object_end))
                        if (!wasm_runtime_call_wasm(exec_env, func, 0, NULL))
                            success = false;
                break;

            case WJB_KEY:
                if (success)
                    if ((func = ctx->json_parse_object_field_start)) {
                        Assert(jbv.type == jbvString);
                        wasm_externref_obj_t key =
                            cstring_into_varatt_obj(exec_env,
                                                    jbv.val.string.val,
                                                    jbv.val.string.len,
                                                    TEXTOID);
                        wasm_val_t arg = { .kind = WASM_EXTERNREF,
                                           .of.foreign = key };
                        if (!wasm_runtime_call_wasm_a(exec_env,
                                                      func,
                                                      0,
                                                      NULL,
                                                      1,
                                                      &arg))
                            success = false;
                    }
                break;

            case WJB_VALUE:
            case WJB_ELEM:
                if (success)
                    switch (jbv.type) {
                        case jbvString:
                            if ((func = ctx->json_parse_push_string)) {
                                wasm_externref_obj_t str =
                                    cstring_into_varatt_obj(exec_env,
                                                            jbv.val.string.val,
                                                            jbv.val.string.len,
                                                            TEXTOID);
                                wasm_val_t arg = { .kind = WASM_EXTERNREF,
                                                   .of.foreign = str };
                                if (!wasm_runtime_call_wasm_a(exec_env,
                                                              func,
                                                              0,
                                                              NULL,
                                                              1,
                                                              &arg))
                                    success = false;
                            }
                            break;

                        case jbvNumeric:
                            if ((func = ctx->json_parse_push_number)) {
                                Datum num = DirectFunctionCall1(
                                    numeric_float8,
                                    NumericGetDatum(jbv.val.numeric));
                                wasm_val_t arg = { .kind = WASM_F64,
                                                   .of.f64 =
                                                       DatumGetFloat8(num) };
                                if (!wasm_runtime_call_wasm_a(exec_env,
                                                              func,
                                                              0,
                                                              NULL,
                                                              1,
                                                              &arg))
                                    success = false;
                            }
                            break;

                        case jbvBool:
                            if ((func = ctx->json_parse_push_bool)) {
                                wasm_val_t arg = { .kind = WASM_I32,
                                                   .of.i32 = jbv.val.boolean };
                                if (!wasm_runtime_call_wasm_a(exec_env,
                                                              func,
                                                              0,
                                                              NULL,
                                                              1,
                                                              &arg))
                                    success = false;
                            }
                            break;

                        case jbvNull:
                            if ((func = ctx->json_parse_push_null))
                                if (!wasm_runtime_call_wasm(exec_env,
                                                            func,
                                                            0,
                                                            NULL))
                                    success = false;
                            break;

                        default:
                            ereport(ERROR,
                                    errmsg("rst_jsonb_iterate: invalid type"));
                    }
                break;

            case WJB_DONE:
                return success;
        }
    }
}

static NativeSymbol jsonb_symbols[] = {
    { "jsonb_in", rsl_jsonb_in, "(r)r" },
    { "jsonb_out", rsl_jsonb_out, "(r)r" },
    { "jsonb_value_to_jsonb", jsonb_value_to_jsonb, "(r)r" },
    { "jsonb_value_new_null", jsonb_value_new_null, "()r" },
    { "jsonb_value_new_string", jsonb_value_new_string, "(r)r" },
    { "jsonb_value_new_numeric", jsonb_value_new_numeric, "(F)r" },
    { "jsonb_value_new_bool", jsonb_value_new_bool, "(i)r" },
    { "jsonb_value_new_array", jsonb_value_new_array, "(i)r" },
    { "jsonb_value_array_set", jsonb_value_array_set, "(rir)i" },
    { "jsonb_value_new_object", jsonb_value_new_object, "(i)r" },
    { "jsonb_value_object_set", jsonb_value_object_set, "(rirr)i" },
    { "json_parse", rst_json_parse, "(rii)i" },
    { "jsonb_iterate", rst_jsonb_iterate, "(r)i" },
};

void
rst_register_natives_jsonb() {
    REGISTER_WASM_NATIVES("env", jsonb_symbols);
}

void
rst_init_context_for_jsonb(wasm_exec_env_t exec_env) {
    Context *ctx = (Context *)wasm_runtime_get_user_data(exec_env);

    WASMArrayType *arr_type =
        (WASMArrayType *)palloc0(sizeof(WASMArrayType) + sizeof(WASMRttType));
    arr_type->base_type.type_flag = WASM_TYPE_ARRAY;
    arr_type->elem_type = REF_TYPE_ANYREF;
    WASMType *defined_type = (WASMType *)arr_type;
    WASMRttTypeRef anyref_array = (WASMRttTypeRef)(arr_type + 1);
    anyref_array->type_flag = defined_type->type_flag;
    anyref_array->inherit_depth = defined_type->inherit_depth;
    anyref_array->defined_type = defined_type;
    anyref_array->root_type = defined_type->root_type;
    ctx->anyref_array = anyref_array;

    wasm_module_inst_t instance = wasm_exec_env_get_module_inst(exec_env);
    wasm_function_inst_t func;
    if ((func =
             wasm_runtime_lookup_function(instance, "json_parse_push_string")))
        ctx->json_parse_push_string = func;
    if ((func =
             wasm_runtime_lookup_function(instance, "json_parse_push_number")))
        ctx->json_parse_push_number = func;
    if ((func = wasm_runtime_lookup_function(instance, "json_parse_push_bool")))
        ctx->json_parse_push_bool = func;
    if ((func = wasm_runtime_lookup_function(instance, "json_parse_push_null")))
        ctx->json_parse_push_null = func;
    if ((func =
             wasm_runtime_lookup_function(instance, "json_parse_object_start")))
        ctx->json_parse_object_start = func;
    if ((func = wasm_runtime_lookup_function(instance,
                                             "json_parse_object_field_start")))
        ctx->json_parse_object_field_start = func;
    if ((func =
             wasm_runtime_lookup_function(instance, "json_parse_object_end")))
        ctx->json_parse_object_end = func;
    if ((func =
             wasm_runtime_lookup_function(instance, "json_parse_array_start")))
        ctx->json_parse_array_start = func;
    if ((func = wasm_runtime_lookup_function(instance, "json_parse_array_end")))
        ctx->json_parse_array_end = func;
}
