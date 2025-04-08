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

#ifndef RUSTICA_DATATYPES_H
#define RUSTICA_DATATYPES_H

#include "postgres.h"
#include "utils/jsonb.h"

#include "wasm_runtime_common.h"
#include "executor/spi.h"

#define REGISTER_WASM_NATIVES(name, natives)                        \
    if (!wasm_runtime_register_natives(name,                        \
                                       natives,                     \
                                       sizeof(natives)              \
                                           / sizeof((natives)[0]))) \
        ereport(ERROR, errmsg("cannot register WASM natives"));
#define RST_FREE_IF_COPY(ptr, datum)                  \
    do {                                              \
        if ((Pointer)(ptr) != DatumGetPointer(datum)) \
            pfree(ptr);                               \
    } while (0)

#define VARATT_MAX 0x3FFFFFFF

#define OBJ_DATUM 0
#define OBJ_STRING_INFO 1
#define OBJ_JSONB_VALUE 2
#define OBJ_PORTAL 3
#define OBJ_TUPLE_TABLE 4
#define OBJ_HEAP_TUPLE 5

#define OBJ_REFERENCING (1 << 0)
#define OBJ_OWNS_BODY (1 << 1)
#define OBJ_OWNS_BODY_MEMBERS (1 << 2)

typedef uint16_t ObjType;

typedef struct Obj {
    // 32-bit header
    ObjType type;
    uint16_t flags;

    // optional 32-bit member
    union {
        Oid oid;           // only for OBJ_DATUM
        int32_t query_idx; // OBJ_PORTAL, OBJ_TUPLE_TABLE
    };

    // pointer-sized body
    union {
        Datum datum;             // only for OBJ_DATUM
        StringInfo sb;           // only for OBJ_STRING_INFO
        JsonbValue *jbv;         // only for OBJ_JSONB_VALUE
        Portal portal;           // only for OBJ_PORTAL
        SPITupleTable *tuptable; // only for OBJ_TUPLE_TABLE
        HeapTuple tuple;         // only for OBJ_HEAP_TUPLE

        void *ptr; // convenient compatible pointer for all types
    } body;

    // Optional reference to another object, and trailing embedded data
    wasm_local_obj_ref_t ref[]; // only when OBJ_REFERENCING is set
} Obj, *obj_t;

wasm_externref_obj_t
rst_externref_of_obj(wasm_exec_env_t exec_env, obj_t obj);

wasm_obj_t
rst_anyref_of_obj(wasm_exec_env_t exec_env, obj_t obj);

obj_t
rst_obj_new(wasm_exec_env_t exec_env,
            ObjType type,
            wasm_obj_t ref,
            size_t embed_size);

WASMValue *
rst_obj_new_static(ObjType type, obj_t *obj_out, size_t embed_size);

obj_t
wasm_externref_obj_get_obj(wasm_obj_t refobj, ObjType type);

Datum
wasm_externref_obj_get_datum(wasm_obj_t refobj, Oid oid);

char *
wasm_text_copy_cstring(wasm_obj_t refobj);

wasm_externref_obj_t
rst_externref_of_owned_datum(wasm_exec_env_t exec_env, Datum datum, Oid oid);

wasm_externref_obj_t
cstring_into_varatt_obj(wasm_exec_env_t exec_env,
                        const void *data,
                        size_t llen,
                        Oid oid);

void
rst_register_natives_bytea();

void
rst_register_natives_date();

void
rst_register_natives_jsonb();

void
rst_register_natives_json();

void
rst_register_natives_primitives();

void
rst_register_natives_stringbuilder();

void
rst_register_natives_text();

void
rst_register_natives_timestamp();

void
rst_register_natives_uuid();

void
rst_init_context_for_jsonb(wasm_exec_env_t exec_env);

#endif /* RUSTICA_DATATYPES_H */
