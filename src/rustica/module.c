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
#include "executor/spi.h"
#include "tcop/tcopprot.h"
#include "utils/builtins.h"

#include "rustica/module.h"

static SPIPlanPtr load_module_plan = NULL;
static const char *load_module_sql =
    "SELECT bin_code FROM rustica.modules WHERE name = $1";

void
rst_module_worker_startup() {
    debug_query_string = load_module_sql;
    load_module_plan = SPI_prepare(load_module_sql, 1, (Oid[1]){ TEXTOID });

    if (!load_module_plan)
        ereport(ERROR,
                errmsg("could not prepare SPI plan: %s",
                       SPI_result_code_string(SPI_result)));
    if (SPI_keepplan(load_module_plan))
        ereport(ERROR, errmsg("failed to keep plan"));
    debug_query_string = NULL;
}

void
rst_module_worker_teardown() {
    SPI_freeplan(load_module_plan);
}

void
rst_load_module(const char *name, uint8 **buffer, uint32 *size) {
    Datum name_datum = CStringGetTextDatum(name);

    PG_TRY();
    {
        debug_query_string = load_module_sql;
        int ret =
            SPI_execute_plan(load_module_plan, &name_datum, NULL, true, 1);
        if (ret != SPI_OK_SELECT)
            ereport(ERROR,
                    errmsg("failed to load module \"%s\": %s",
                           name,
                           SPI_result_code_string(ret)));
        if (SPI_processed == 0)
            ereport(ERROR,
                    errcode(ERRCODE_NO_DATA_FOUND),
                    errmsg("module \"%s\" doesn't exist", name));

        bool isnull;
        bytea *bin_code = DatumGetByteaPP(SPI_getbinval(SPI_tuptable->vals[0],
                                                        SPI_tuptable->tupdesc,
                                                        1,
                                                        &isnull));
        Assert(!isnull);

        *buffer = (uint8 *)VARDATA_ANY(bin_code);
        *size = VARSIZE_ANY_EXHDR(bin_code);
        Assert(*buffer != NULL);
    }
    PG_FINALLY();
    {
        pfree(DatumGetPointer(name_datum));
        debug_query_string = NULL;
    }
    PG_END_TRY();
}
