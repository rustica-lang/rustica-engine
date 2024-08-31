/*
 * Copyright (c) 2024 燕几（北京）科技有限公司
 * Rustica (runtime) is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan
 * PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "postgres.h"
#include <string.h>
#include <sys/un.h>
#include "fmgr.h"
#include "varatt.h"
#include "wasm_memory.h"
#include "aot_export.h"
#include "postmaster/bgworker.h"
#include "rustica_wamr.h"
#if WASM_ENABLE_DEBUG_AOT != 0
#include "storage/fd.h"
#include "dwarf_extractor.h"
#endif

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
    char error_buf[128];
    wasm_module_t wasm_module = NULL;
    aot_comp_data_t comp_data = NULL;
    aot_comp_context_t comp_ctx = NULL;
    AOTCompOption option = { 0 };
    option.opt_level = 3;
    option.size_level = 0;
    option.output_format = AOT_FORMAT_FILE;
    option.bounds_checks = 2;
    option.stack_bounds_checks = 2;
    option.enable_simd = true;
    option.enable_bulk_memory = true;
#if WASM_ENABLE_DUMP_CALL_STACK != 0
    option.enable_aux_stack_frame = true;
#endif
#if WASM_ENABLE_GC != 0
    option.enable_gc = true;
#else
    option.enable_ref_types = true;
#endif
    option.target_arch = "x86_64";

    bytea *wasm = PG_GETARG_BYTEA_PP(0);
    int32 wasm_size = VARSIZE_ANY_EXHDR(wasm);

    wasm_module = wasm_runtime_load((uint8 *)VARDATA_ANY(wasm),
                                    wasm_size,
                                    error_buf,
                                    sizeof(error_buf));
    if (!wasm_module) {
        ereport(ERROR, (errmsg("failed to load WASM module: %s", error_buf)));
        PG_RETURN_NULL();
    }
    comp_data = aot_create_comp_data(wasm_module, option.target_arch, true);
    if (!comp_data) {
        ereport(ERROR,
                (errmsg("could not create compilation data: %s",
                        aot_get_last_error())));
        PG_RETURN_NULL();
    }
#if WASM_ENABLE_DEBUG_AOT != 0
    File file = OpenTemporaryFile(false);
    int nbytes = FileWrite(file, VARDATA_ANY(wasm), wasm_size, 0, 0);
    if (nbytes != wasm_size) {
        if (nbytes < 0)
            ereport(ERROR,
                    (errcode_for_file_access(),
                     errmsg("could not write temporary file: %m")));
        ereport(
            ERROR,
            (errcode(ERRCODE_DISK_FULL),
             errmsg("could not write temporary file: wrote only %d of %d bytes",
                    nbytes,
                    wasm_size)));
    }
    if (!create_dwarf_extractor(comp_data, FilePathName(file))) {
        ereport(ERROR,
                (errmsg("could not create dwarf extractor: %s",
                        aot_get_last_error())));
        PG_RETURN_NULL();
    }
    FileClose(file);
#endif
    comp_ctx = aot_create_comp_context(comp_data, &option);
    if (!comp_ctx) {
        ereport(ERROR,
                (errmsg("could not create compilation context: %s",
                        aot_get_last_error())));
        PG_RETURN_NULL();
    }
    if (!aot_compile_wasm(comp_ctx)) {
        ereport(ERROR,
                (errmsg("failed to compile wasm module: %s",
                        aot_get_last_error())));
        PG_RETURN_NULL();
    }

    aot_obj_data_t obj_data = aot_obj_data_create(comp_ctx);
    if (!obj_data) {
        ereport(
            ERROR,
            (errmsg("could not create object data: %s", aot_get_last_error())));
        PG_RETURN_NULL();
    }
    uint32_t aot_file_size =
        aot_get_aot_file_size(comp_ctx, comp_data, obj_data);
    bytea *rv = (bytea *)palloc(aot_file_size + VARHDRSZ);
    if (!rv) {
        ereport(ERROR,
                (errmsg("could not allocate memory (size=%u) for aot file",
                        aot_file_size)));
        PG_RETURN_NULL();
    }
    SET_VARSIZE(rv, aot_file_size + VARHDRSZ);
    if (!aot_emit_aot_file_buf_ex(comp_ctx,
                                  comp_data,
                                  obj_data,
                                  (uint8 *)VARDATA(rv),
                                  aot_file_size)) {
        ereport(ERROR,
                (errmsg("Failed to emit aot file: %s", aot_get_last_error())));
        PG_RETURN_NULL();
    }
    aot_obj_data_destroy(obj_data);
    PG_RETURN_TEXT_P(rv);
}

void
_PG_fini() {
    aot_compiler_destroy();
    wasm_runtime_memory_destroy();
}
