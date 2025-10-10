// SPDX-FileCopyrightText: 2024 燕几（北京）科技有限公司
// SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0

#ifndef RUSTICA_MODULE_H
#define RUSTICA_MODULE_H

#include "postgres.h"
#include "executor/spi.h"

#include "aot_runtime.h"

#include "rustica/query.h"
#include "rustica/wamr.h"

#define RST_MODULE_NAME_MAXLEN 127

typedef struct PreparedModule {
    char name[RST_MODULE_NAME_MAXLEN + 1];
    AOTModule *module;
    SPITupleTable *loading_tuptable;
    CommonHeapTypes heap_types;
    int nqueries;
    QueryPlan queries[];
} PreparedModule;

void
rst_module_worker_startup();

void
rst_module_worker_teardown();

PreparedModule *
rst_prepare_module(const char *name, uint8 **buffer, uint32 *size);

PreparedModule *
rst_lookup_module(const char *name);

void
rst_free_module(PreparedModule *pmod);

wasm_exec_env_t
rst_module_instantiate(PreparedModule *pmod,
                       uint32 stack_size,
                       uint32 heap_size);

#endif /* RUSTICA_MODULE_H */
