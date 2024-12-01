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

#include "postgres.h"
#include "fmgr.h"
#include "postmaster/bgworker.h"
#include "funcapi.h"

#include "rustica/compiler.h"
#include "rustica/gucs.h"
#include "rustica/main.h"
#include "rustica/wamr.h"

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

    MemoryContext tx_mctx = MemoryContextSwitchTo(TopMemoryContext);
    rst_init_wamr();
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
    rst_fini_wamr();
}
