/*
 * SPDX-FileCopyrightText: 2025 燕几（北京）科技有限公司
 * SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0
 */

#include "postgres.h"
#include "catalog/pg_type_d.h"
#include "utils/date.h"
#include "utils/fmgrprotos.h"

#include "wasm_runtime_common.h"
#include "rustica/datatypes.h"

static int32_t
rst_date_in(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    char *pgstr = wasm_text_copy_cstring(refobj);
    Datum rv = DirectFunctionCall1(date_in, CStringGetDatum(pgstr));
    pfree(pgstr);
    return DatumGetDateADT(rv);
}

static wasm_externref_obj_t
rst_date_out(wasm_exec_env_t exec_env, int32_t date) {
    char *pgstr =
        DatumGetCString(DirectFunctionCall1(date_out, DateADTGetDatum(date)));
    wasm_externref_obj_t rv =
        cstring_into_varatt_obj(exec_env, pgstr, strlen(pgstr), TEXTOID);
    pfree(pgstr);
    return rv;
}

static int64_t
rst_time_in(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    char *pgstr = wasm_text_copy_cstring(refobj);
    Datum rv = DirectFunctionCall1(time_in, CStringGetDatum(pgstr));
    pfree(pgstr);
    return DatumGetTimeADT(rv);
}

static wasm_externref_obj_t
rst_time_out(wasm_exec_env_t exec_env, int64_t time) {
    char *pgstr =
        DatumGetCString(DirectFunctionCall1(time_out, TimeADTGetDatum(time)));
    wasm_externref_obj_t rv =
        cstring_into_varatt_obj(exec_env, pgstr, strlen(pgstr), TEXTOID);
    pfree(pgstr);
    return rv;
}

static NativeSymbol date_symbols[] = {
    { "date_in", rst_date_in, "(r)i" },
    { "date_out", rst_date_out, "(i)r" },
    { "time_in", rst_time_in, "(r)I" },
    { "time_out", rst_time_out, "(I)r" },
};

void
rst_register_natives_date() {
    REGISTER_WASM_NATIVES("env", date_symbols);
}
