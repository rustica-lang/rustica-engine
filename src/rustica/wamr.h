/*
 * Copyright (c) 2024-present 燕几（北京）科技有限公司
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

#ifndef RUSTICA_WAMR_H
#define RUSTICA_WAMR_H

#include "postgres.h"
#include "utils/uuid.h"

#include "wasm_runtime_common.h"

typedef struct TidOid {
    pg_uuid_t *tid;
    Oid oid;
} TidOid;

extern TidOid *tid_map;
extern int tid_map_len;

extern NativeSymbol rst_noop_native_env[];

typedef struct CommonHeapTypes {
    int32_t bytes;
} CommonHeapTypes;

void
rst_init_wamr();

void
rst_fini_wamr();

wasm_struct_type_t
wasm_ref_type_get_referred_struct(wasm_ref_type_t ref_type,
                                  wasm_module_t module,
                                  bool nullable);

wasm_array_type_t
wasm_ref_type_get_referred_array(wasm_ref_type_t ref_type,
                                 wasm_module_t module,
                                 bool nullable);

bool
wasm_ref_type_is_ref_extern(wasm_ref_type_t ref_type);

wasm_func_type_t
wasm_module_lookup_exported_func(wasm_module_t module, const char *name);

const char *
wasm_ref_type_repr(CommonHeapTypes *heap_types, wasm_ref_type_t ref_type);

void
wasm_runtime_unregister_and_unload(wasm_module_t module);

void
wasm_runtime_remove_local_obj_ref(wasm_exec_env_t exec_env,
                                  wasm_local_obj_ref_t *me);

int32_t
env_ereport(wasm_exec_env_t exec_env, int32_t level, wasm_obj_t ref);

int32_t
env_tid_to_oid(wasm_exec_env_t exec_env, wasm_obj_t obj);

#endif /* RUSTICA_WAMR_H */
