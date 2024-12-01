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

#include <sys/socket.h>
#include <sys/un.h>
#include <wchar.h>

#include "postgres.h"
#include "fmgr.h"
#include "postmaster/bgworker.h"
#include "funcapi.h"

#include "compiler.h"
#include "gucs.h"
#include "main.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(compile_wasm);

static void
native_noop(wasm_exec_env_t exec_env) {}

NativeSymbol noop_native_env[] = {
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

static void
spectest_print_char(wasm_exec_env_t exec_env, int c) {
    fwprintf(stderr, L"%lc", c);
}

static NativeSymbol spectest[] = {
    { "print_char", spectest_print_char, "(i)" }
};

void
make_ipc_addr(struct sockaddr_un *addr) {
    memset(addr, 0, sizeof(struct sockaddr_un));
    addr->sun_family = AF_UNIX;
    addr->sun_path[0] = '\0';
    snprintf(&addr->sun_path[1], sizeof(addr->sun_path) - 1, "rustica-ipc");
}

void
_PG_init() {
    rst_init_gucs();

    // Initialize WAMR runtime with native stubs
    RuntimeInitArgs init_args = { .mem_alloc_type = Alloc_With_Allocator,
                                  .mem_alloc_option = {
                                      .allocator.malloc_func = palloc,
                                      .allocator.realloc_func = repalloc,
                                      .allocator.free_func = pfree,
                                  },
                                  .gc_heap_size = 16 * 1024 * 1024 };
    MemoryContext tx_mctx = MemoryContextSwitchTo(TopMemoryContext);
    if (!wasm_runtime_full_init(&init_args))
        ereport(FATAL, (errmsg("cannot register WASM natives")));
    if (!wasm_runtime_register_natives("spectest",
                                       spectest,
                                       sizeof(spectest) / sizeof(spectest[0])))
        ereport(ERROR, errmsg("cannot register WASM natives"));
    if (!wasm_runtime_register_natives("env",
                                       noop_native_env,
                                       sizeof(noop_native_env)
                                           / sizeof(noop_native_env[0])))
        ereport(ERROR, errmsg("cannot instantiate WASM module"));
    MemoryContextSwitchTo(tx_mctx);

    // Start up the Rustica master process
    BackgroundWorker master = { .bgw_flags = BGWORKER_SHMEM_ACCESS,
                                .bgw_start_time = BgWorkerStart_PostmasterStart,
                                .bgw_restart_time = 10,
                                .bgw_notify_pid = 0 };
    snprintf(master.bgw_name, BGW_MAXLEN, "rustica master");
    snprintf(master.bgw_type, BGW_MAXLEN, "rustica master");
    snprintf(master.bgw_library_name, BGW_MAXLEN, "rustica-wamr");
    snprintf(master.bgw_function_name, BGW_MAXLEN, "rustica_master");
    RegisterBackgroundWorker(&master);
}

Datum
compile_wasm(PG_FUNCTION_ARGS) {
    return rst_compile(fcinfo);
}

void
_PG_fini() {
    wasm_runtime_destroy();
}
