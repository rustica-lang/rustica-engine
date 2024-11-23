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

#include <sys/un.h>

#include "postgres.h"
#include "fmgr.h"
#include "postmaster/bgworker.h"

#include "wasm_memory.h"
#include "aot_export.h"

#include "compiler.h"
#include "gucs.h"
#include "rustica_wamr.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(compile_wasm);

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

    if (!wasm_runtime_init()) {
        ereport(FATAL, (errmsg("cannot initialize WAMR runtime")));
    }

    BackgroundWorker master;
    snprintf(master.bgw_name, BGW_MAXLEN, "rustica master");
    snprintf(master.bgw_type, BGW_MAXLEN, "rustica master");
    master.bgw_flags = BGWORKER_SHMEM_ACCESS;
    master.bgw_start_time = BgWorkerStart_PostmasterStart;
    master.bgw_restart_time = 10;
    snprintf(master.bgw_library_name, BGW_MAXLEN, "rustica-wamr");
    snprintf(master.bgw_function_name, BGW_MAXLEN, "rustica_master");
    master.bgw_notify_pid = 0;
    RegisterBackgroundWorker(&master);
}

Datum
compile_wasm(PG_FUNCTION_ARGS) {
    return rst_compile(fcinfo);
}

void
_PG_fini() {
    aot_compiler_destroy();
    wasm_runtime_memory_destroy();
}
