/*
 * SPDX-FileCopyrightText: 2024 燕几（北京）科技有限公司
 * SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0
 */

#include "postgres.h"
#include "postmaster/bgworker.h"
#include "utils/memutils.h"

#include "rustica/compiler.h"
#include "rustica/gucs.h"
#include "rustica/wamr.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(compile_wasm);

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
    snprintf(master.bgw_library_name, BGW_MAXLEN, "rustica-engine");
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
