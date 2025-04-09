/*
 * Copyright (c) 2024-present 燕几（北京）科技有限公司
 *
 * Rustica (runtime) is licensed under Mulan PSL v2. You can use this
 * software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *              https://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
 * OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the Mulan PSL v2 for more details.
 */

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
