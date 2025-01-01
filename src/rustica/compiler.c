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

#include "postgres.h"
#include "varatt.h"

#include "wasm_memory.h"
#include "aot_export.h"

#if WASM_ENABLE_DEBUG_AOT != 0
#include "storage/fd.h"
#include "dwarf_extractor.h"
#endif

#include "rustica/compiler.h"
#include "rustica/utils.h"

Datum
rst_compile(PG_FUNCTION_ARGS) {
    DECLARE_ERROR_BUF(128);
    wasm_module_t wasm_module = NULL;
    aot_comp_data_t comp_data = NULL;
    aot_comp_context_t comp_ctx = NULL;
    AOTCompOption option = {
        .opt_level = 3,
        .size_level = 3,
        .output_format = AOT_FORMAT_FILE,
        .bounds_checks = 2,
        .stack_bounds_checks = 2,
        .enable_simd = true,
        .enable_bulk_memory = true,
#if WASM_ENABLE_DUMP_CALL_STACK != 0
        .enable_aux_stack_frame = true,
#endif
#if WASM_ENABLE_GC != 0
        .enable_gc = true,
#else
        .enable_ref_types = true,
#endif
        .target_arch = "x86_64",
    };

    bytea *wasm = PG_GETARG_BYTEA_PP(0);
    int32 wasm_size = VARSIZE_ANY_EXHDR(wasm);

    wasm_module = wasm_runtime_load((uint8 *)VARDATA_ANY(wasm),
                                    wasm_size,
                                    ERROR_BUF_PARAMS);
    if (!wasm_module)
        ereport(ERROR, errmsg("failed to load WASM module: %s", ERROR_BUF));
    comp_data = aot_create_comp_data(wasm_module, option.target_arch, true);
    if (!comp_data)
        ereport(ERROR,
                errmsg("could not create compilation data: %s",
                       aot_get_last_error()));
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
                errmsg("could not create dwarf extractor: %s",
                       aot_get_last_error()));
        PG_RETURN_NULL();
    }
    FileClose(file);
#endif
    comp_ctx = aot_create_comp_context(comp_data, &option);
    if (!comp_ctx)
        ereport(ERROR,
                errmsg("could not create compilation context: %s",
                       aot_get_last_error()));
    if (!aot_compile_wasm(comp_ctx))
        ereport(
            ERROR,
            errmsg("failed to compile wasm module: %s", aot_get_last_error()));

    aot_obj_data_t obj_data = aot_obj_data_create(comp_ctx);
    if (!obj_data)
        ereport(
            ERROR,
            errmsg("could not create object data: %s", aot_get_last_error()));
    uint32_t aot_file_size =
        aot_get_aot_file_size(comp_ctx, comp_data, obj_data);
    bytea *rv = (bytea *)palloc(aot_file_size + VARHDRSZ);
    if (!rv)
        ereport(ERROR,
                errmsg("could not allocate memory (size=%u) for aot file",
                       aot_file_size));
    SET_VARSIZE(rv, aot_file_size + VARHDRSZ);
    if (!aot_emit_aot_file_buf_ex(comp_ctx,
                                  comp_data,
                                  obj_data,
                                  (uint8 *)VARDATA(rv),
                                  aot_file_size))
        ereport(ERROR,
                errmsg("Failed to emit aot file: %s", aot_get_last_error()));
    aot_obj_data_destroy(obj_data);
    PG_RETURN_TEXT_P(rv);
}
