// SPDX-FileCopyrightText: 2025 燕几（北京）科技有限公司
// SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0

#include "postgres.h"
#include "catalog/pg_type_d.h"
#include "utils/fmgrprotos.h"

#include "wasm_runtime_common.h"
#include "rustica/datatypes.h"

static int64_t
rsl_timestamp_in(wasm_exec_env_t exec_env, wasm_obj_t refobj, int32_t tz) {
    char *pgstr = wasm_text_copy_cstring(refobj);
    Datum ts = DirectFunctionCall3(tz ? timestamptz_in : timestamp_in,
                                   CStringGetDatum(pgstr),
                                   ObjectIdGetDatum(InvalidOid),
                                   Int32GetDatum(-1));
    pfree(pgstr);
    return DatumGetInt64(ts);
}

static wasm_externref_obj_t
rsl_timestamp_out(wasm_exec_env_t exec_env, int64_t ts, int32_t tz) {
    char *pgstr = DatumGetCString(
        DirectFunctionCall1(tz ? timestamptz_out : timestamp_out,
                            Int64GetDatum(ts)));
    wasm_externref_obj_t rv =
        cstring_into_varatt_obj(exec_env, pgstr, strlen(pgstr), TEXTOID);
    pfree(pgstr);
    return rv;
}

static int64_t
rsl_timestamp_pl_interval(wasm_exec_env_t exec_env,
                          int64_t timestamp,
                          wasm_obj_t interval) {
    Datum ts = Int64GetDatum(timestamp);
    Datum interval_datum = wasm_externref_obj_get_datum(interval, INTERVALOID);
    return DatumGetInt64(
        DirectFunctionCall2(timestamp_pl_interval, ts, interval_datum));
}

static int64_t
rsl_timestamp_mi_interval(wasm_exec_env_t exec_env,
                          int64_t timestamp,
                          wasm_obj_t interval) {
    Datum ts = Int64GetDatum(timestamp);
    Datum interval_datum = wasm_externref_obj_get_datum(interval, INTERVALOID);
    return DatumGetInt64(
        DirectFunctionCall2(timestamp_mi_interval, ts, interval_datum));
}

static wasm_externref_obj_t
rst_interval_in(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    char *pgstr = wasm_text_copy_cstring(refobj);
    Datum rv = DirectFunctionCall3(interval_in,
                                   CStringGetDatum(pgstr),
                                   InvalidOid,
                                   Int32GetDatum(-1));
    pfree(pgstr);
    return rst_externref_of_owned_datum(exec_env, rv, INTERVALOID);
}

static wasm_externref_obj_t
rst_interval_make(wasm_exec_env_t exec_env,
                  int32_t years,
                  int32_t months,
                  int32_t weeks,
                  int32_t days,
                  int32_t hours,
                  int32_t minutes,
                  double_t seconds) {
    Datum rv = DirectFunctionCall7(make_interval,
                                   Int32GetDatum(years),
                                   Int32GetDatum(months),
                                   Int32GetDatum(weeks),
                                   Int32GetDatum(days),
                                   Int32GetDatum(hours),
                                   Int32GetDatum(minutes),
                                   Float8GetDatum(seconds));
    return rst_externref_of_owned_datum(exec_env, rv, INTERVALOID);
}

static wasm_externref_obj_t
rst_interval_out(wasm_exec_env_t exec_env, wasm_obj_t refobj) {
    Datum interval = wasm_externref_obj_get_datum(refobj, INTERVALOID);
    char *pgstr = DatumGetCString(DirectFunctionCall1(interval_out, interval));
    wasm_externref_obj_t rv =
        cstring_into_varatt_obj(exec_env, pgstr, strlen(pgstr), TEXTOID);
    pfree(pgstr);
    return rv;
}

static wasm_externref_obj_t
rst_interval_pl(wasm_exec_env_t exec_env,
                wasm_obj_t refobj1,
                wasm_obj_t refobj2) {
    Datum interval1 = wasm_externref_obj_get_datum(refobj1, INTERVALOID);
    Datum interval2 = wasm_externref_obj_get_datum(refobj2, INTERVALOID);
    Datum rv = DirectFunctionCall2(interval_pl, interval1, interval2);
    return rst_externref_of_owned_datum(exec_env, rv, INTERVALOID);
}

static wasm_externref_obj_t
rst_interval_mi(wasm_exec_env_t exec_env,
                wasm_obj_t refobj1,
                wasm_obj_t refobj2) {
    Datum interval1 = wasm_externref_obj_get_datum(refobj1, INTERVALOID);
    Datum interval2 = wasm_externref_obj_get_datum(refobj2, INTERVALOID);
    Datum rv = DirectFunctionCall2(interval_mi, interval1, interval2);
    return rst_externref_of_owned_datum(exec_env, rv, INTERVALOID);
}

static wasm_externref_obj_t
rsl_interval_mul(wasm_exec_env_t exec_env, wasm_obj_t refobj, double_t factor) {
    Datum interval = wasm_externref_obj_get_datum(refobj, INTERVALOID);
    Datum rv =
        DirectFunctionCall2(interval_mul, interval, Float8GetDatum(factor));
    return rst_externref_of_owned_datum(exec_env, rv, INTERVALOID);
}

static wasm_externref_obj_t
rsl_interval_div(wasm_exec_env_t exec_env,
                 wasm_obj_t refobj,
                 double_t divisor) {
    Datum interval = wasm_externref_obj_get_datum(refobj, INTERVALOID);
    Datum rv =
        DirectFunctionCall2(interval_div, interval, Float8GetDatum(divisor));
    return rst_externref_of_owned_datum(exec_env, rv, INTERVALOID);
}

static NativeSymbol timestamp_natives[] = {
    { "timestamp_in", rsl_timestamp_in, "(ri)I" },
    { "timestamp_out", rsl_timestamp_out, "(Ii)r" },
    { "timestamp_pl_interval", rsl_timestamp_pl_interval, "(Ir)I" },
    { "timestamp_mi_interval", rsl_timestamp_mi_interval, "(Ir)I" },
    { "interval_in", rst_interval_in, "(r)r" },
    { "interval_make", rst_interval_make, "(iiiiiiF)r" },
    { "interval_out", rst_interval_out, "(r)r" },
    { "interval_pl", rst_interval_pl, "(rr)r" },
    { "interval_mi", rst_interval_mi, "(rr)r" },
    { "interval_mul", rsl_interval_mul, "(ri)I" },
    { "interval_div", rsl_interval_div, "(ri)I" },
};

void
rst_register_natives_timestamp() {
    REGISTER_WASM_NATIVES("env", timestamp_natives);
}
