/*
 * Copyright (c) 2024 燕几（北京）科技有限公司
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

#include <wchar.h>

#include "postgres.h"

#include "rustica/wamr.h"

static void
spectest_print_char(wasm_exec_env_t exec_env, int c) {
    fwprintf(stderr, L"%lc", c);
}

static NativeSymbol spectest[] = {
    { "print_char", spectest_print_char, "(i)" }
};

static void
native_noop(wasm_exec_env_t exec_env) {}

NativeSymbol rst_noop_native_env[] = {
    { "recv", native_noop, "(rii)i" },
    { "send", native_noop, "(rii)i" },
    { "llhttp_execute", native_noop, "(rii)i" },
    { "llhttp_resume", native_noop, "()i" },
    { "llhttp_finish", native_noop, "(r)i" },
    { "llhttp_reset", native_noop, "()i" },
    { "llhttp_get_error_pos", native_noop, "(r)i" },
    { "llhttp_get_method", native_noop, "()i" },
    { "llhttp_get_http_major", native_noop, "()i" },
    { "llhttp_get_http_minor", native_noop, "()i" },
    { "execute_statement", native_noop, "(i)i" },
    { "detoast", native_noop, "(iii)r" },
};

void
rst_init_wamr() {
    // Initialize WAMR runtime with native stubs
    RuntimeInitArgs init_args = { .mem_alloc_type = Alloc_With_Allocator,
                                  .mem_alloc_option = {
                                      .allocator.malloc_func = palloc,
                                      .allocator.realloc_func = repalloc,
                                      .allocator.free_func = pfree,
                                  },
                                  .gc_heap_size = 16 * 1024 * 1024 };
    if (!wasm_runtime_full_init(&init_args))
        ereport(FATAL, (errmsg("cannot register WASM natives")));
    if (!wasm_runtime_register_natives("spectest",
                                       spectest,
                                       sizeof(spectest) / sizeof(spectest[0])))
        ereport(ERROR, errmsg("cannot register WASM natives"));
    if (!wasm_runtime_register_natives("env",
                                       rst_noop_native_env,
                                       sizeof(rst_noop_native_env)
                                           / sizeof(rst_noop_native_env[0])))
        ereport(ERROR, errmsg("cannot instantiate WASM module"));
}

void
rst_fini_wamr() {
    wasm_runtime_destroy();
}
