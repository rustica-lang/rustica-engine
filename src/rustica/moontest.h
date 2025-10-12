// SPDX-FileCopyrightText: 2025 燕几（北京）科技有限公司
// SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0

#ifndef RUSTICA_MOONTEST_H
#define RUSTICA_MOONTEST_H

bool
moontest_parse_args(int argc,
                    char *argv[],
                    const char **out_spec,
                    const char **out_wasm_file);

void
moontest_run(wasm_exec_env_t exec_env, const char *moontest_spec);

#endif /* RUSTICA_MOONTEST_H */
