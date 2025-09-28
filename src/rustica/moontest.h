#ifndef RUSTICA_MOONTEST_H
#define RUSTICA_MOONTEST_H

int
moontest_parse_args(int argc,
                    char *argv[],
                    const char **moontest_spec,
                    const char **wasm_file);

void
moontest_run(wasm_exec_env_t exec_env, const char *moontest_spec);

#endif /* RUSTICA_MOONTEST_H */
