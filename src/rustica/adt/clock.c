// SPDX-FileCopyrightText: 2025 燕几（北京）科技有限公司
// SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0

#include "postgres.h"

#include "rustica/datatypes.h"

static uint64_t
realtime_micros_since_unix_epoch(wasm_exec_env_t exec_env) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static wasm_externref_obj_t
monotonic_now(wasm_exec_env_t exec_env) {
    obj_t mono = rst_obj_new(exec_env, OBJ_CLOCK_MONOTONIC, NULL, 0);
    INSTR_TIME_SET_CURRENT(mono->body.instr_time);
    return rst_externref_of_obj(exec_env, mono);
}

static uint64_t
monotonic_nanos_since(wasm_exec_env_t exec_env, wasm_obj_t ref) {
    obj_t mono = wasm_externref_obj_get_obj(ref, OBJ_CLOCK_MONOTONIC);
    instr_time it;
    INSTR_TIME_SET_CURRENT(it);
    INSTR_TIME_SUBTRACT(it, mono->body.instr_time);
    return INSTR_TIME_GET_NANOSEC(it);
}

static NativeSymbol clock_natives[] = {
    { "realtime_micros_since_unix_epoch",
      realtime_micros_since_unix_epoch,
      "()I" },
    { "monotonic_now", monotonic_now, "()r" },
    { "monotonic_nanos_since", monotonic_nanos_since, "(r)I" },
};

void
rst_register_natives_clock() {
    REGISTER_WASM_NATIVES("env", clock_natives);
}
