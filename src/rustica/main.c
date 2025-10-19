// SPDX-FileCopyrightText: 2025 燕几（北京）科技有限公司
// SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0

#include "postgres.h"
#include "getopt_long.h"
#include "catalog/pg_collation_d.h"
#include "mb/pg_wchar.h"
#include "utils/memutils.h"
#include "utils/pg_locale.h"
#include "unicode/udata.h"

#include "bh_platform.h"
#include "bh_read_file.h"
#include "gc_export.h"
#include "aot_export.h"

#include "rustica/datatypes.h"
#include "rustica/moontest.h"

#define HEAP_M 512
#define STACK_K 512

const char *progname;

static void
run_start(wasm_exec_env_t exec_env, va_list args);

static void
run_moontest(wasm_exec_env_t exec_env, va_list args);

static void
run_wasm_with(const char *wasm_file,
              bool use_aot,
              bool is_moontest,
              void (*fn)(wasm_exec_env_t, va_list),
              ...);

static void
init_locale(const char *categoryname, int category, const char *locale);

typedef enum Subcommand {
    RUN = 0,
    MOONTEST,
    HELP,
    VERSION,
    BAD_ARGS,
} Subcommand;

static void
help() {
    printf(_("%s is a WebAssembly runtime with PostgreSQL backend.\n\n"),
           progname);
    printf(_("Usage:\n"));
    printf(_("  %s run <wasm_file>\n"), progname);
    printf(_("  %s moontest --spec <JSON> <wasm_file>\n\n"), progname);
    printf(_("Options:\n"));
    printf(_("  -m, --mode     compilation mode: aot, jit\n"));
    printf(_("  -h, --help     show this help, then exit\n"));
    printf(_("  -V, --version  output version information, then exit\n"));
}

static Subcommand
parse_args(int argc, char **argv, bool *out_aot) {
    struct option long_options[] = { { "mode", required_argument, NULL, 'm' },
                                     { "help", no_argument, NULL, 'h' },
                                     { "version", no_argument, NULL, 'V' },
                                     { NULL, 0, NULL, 0 } };

    int c;
    const char *mode = getenv("RUSTICA_ENGINE_MODE");
    if (mode) {
        if (strcasecmp(mode, "aot") == 0)
            *out_aot = true;
        else if (strcasecmp(mode, "jit") == 0)
            *out_aot = false;
        else
            ereport(ERROR,
                    (errmsg("Unknown RUSTICA_ENGINE_MODE value '%s', "
                            "available: aot, jit",
                            mode)));
    }

    while ((c = getopt_long(argc, argv, "+m:hV", long_options, NULL)) != -1) {
        switch (c) {
            case 'm':
                if (!optarg)
                    ereport(ERROR, errmsg("-m requires an argument"));
                if (strcasecmp(optarg, "aot") == 0)
                    *out_aot = true;
                else if (strcasecmp(optarg, "jit") == 0)
                    *out_aot = false;
                else
                    ereport(ERROR,
                            (errmsg("Unknown mode '%s', available: aot, jit",
                                    optarg)));
                break;

            case 'h':
                return HELP;

            case 'V':
                return VERSION;

            default:
                return BAD_ARGS;
        }
    }

    if (optind >= argc)
        return HELP;
    if (strcmp(argv[optind], "run") == 0) {
        optind++;
        return RUN;
    }
    if (strcmp(argv[optind], "moontest") == 0) {
        optind++;
        return MOONTEST;
    }

    help();
    ereport(ERROR, (errmsg("Unsupported command '%s'", argv[optind])));
}

int
main(int argc, char *argv[]) {
    bool use_aot = true;
    int rv = 0;
    UErrorCode ustatus = U_ZERO_ERROR;

    progname = get_progname(argv[0]);

    MemoryContextInit();

    set_pglocale_pgservice(argv[0], PG_TEXTDOMAIN("postgres"));
    init_locale("LC_COLLATE", LC_COLLATE, "");
    init_locale("LC_CTYPE", LC_CTYPE, "");
#ifdef LC_MESSAGES
    init_locale("LC_MESSAGES", LC_MESSAGES, "");
#endif
    init_locale("LC_MONETARY", LC_MONETARY, "C");
    init_locale("LC_NUMERIC", LC_NUMERIC, "C");
    init_locale("LC_TIME", LC_TIME, "C");

    unsetenv("LC_ALL");
    SetDatabaseEncoding(PG_UTF8);

    // Initialize default_locale with ICU collator
    udata_setFileAccess(UDATA_NO_FILES, &ustatus);
    if (U_FAILURE(ustatus)) {
        fprintf(stderr, "ERROR: Failed to set ICU file access mode: %s\n", u_errorName(ustatus));
        return -1;
    }
    default_locale.provider = COLLPROVIDER_ICU;
    default_locale.deterministic = true;
    make_icu_collator("", NULL, &default_locale);

    PG_TRY();
    {
        Subcommand subcmd = parse_args(argc, argv, &use_aot);
        switch (subcmd) {
            case RUN:
                if (optind >= argc)
                    ereport(ERROR, errmsg("No wasm file specified for 'run'"));
                run_wasm_with(argv[optind], use_aot, false, run_start);
                break;

            case MOONTEST:
            {
                const char *moontest_spec, *wasm_file;
                if (moontest_parse_args(argc,
                                        argv,
                                        &moontest_spec,
                                        &wasm_file)) {
                    if (wasm_file)
                        run_wasm_with(wasm_file,
                                      use_aot,
                                      true,
                                      run_moontest,
                                      moontest_spec);
                }
                else {
                    rv = 1;
                }
            } break;

            case HELP:
                help();
                break;

            case VERSION:
                printf("%s (PostgreSQL %s)\n", progname, PG_VERSION);
                break;

            case BAD_ARGS:
                rv = 1;
                break;
        }
    }
    PG_CATCH();
    {
        ErrorData *edata = CopyErrorData();
        fprintf(stderr, "Error: %s\n", edata->message);
        FlushErrorState();
        FreeErrorData(edata);
        rv = 1;
    }
    PG_END_TRY();
    return rv;
}

static void
exception_throw(wasm_exec_env_t exec_env) {
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    wasm_runtime_set_exception(inst, "panic");
}

static NativeSymbol exception_symbols[] = {
    { "throw", exception_throw, "()", NULL },
};

static inline void
init_wamr(bool is_moontest) {
    RuntimeInitArgs init_args = {
        .mem_alloc_type = Alloc_With_Allocator,
        .mem_alloc_option = { .allocator = { .malloc_func = palloc,
                                             .realloc_func = repalloc,
                                             .free_func = pfree } },
        .gc_heap_size = (uint32_t)HEAP_M * 1024 * 1024,
        .running_mode = Mode_LLVM_JIT,
    };

    if (!wasm_runtime_full_init(&init_args))
        ereport(ERROR, errmsg("Failed to initialize WASM runtime"));

    rst_register_natives_text();
    rst_register_natives_stringbuilder();
    rst_register_natives_clock();
    if (is_moontest
        && !wasm_runtime_register_natives("exception",
                                          exception_symbols,
                                          sizeof(exception_symbols)
                                              / sizeof(NativeSymbol)))
        ereport(ERROR, errmsg("Failed to register exception natives"));
}

static inline uint8_t *
read_wasm_file(const char *wasm_file, uint32_t *out_size) {
    uint8_t *buffer;
    const char *dot;

    // 检查文件扩展名是否为 .wasm
    dot = strrchr(wasm_file, '.');
    if (!dot || strcasecmp(dot, ".wasm") != 0) {
        ereport(ERROR, (errmsg("Unsupported file type: %s", wasm_file)));
    }

    buffer = (uint8_t *)bh_read_file_to_buffer(wasm_file, out_size);
    if (!buffer) {
        ereport(ERROR, (errmsg("Failed to read wasm file: %s", wasm_file)));
    }
    return buffer;
}

static inline wasm_module_t
load_wasm_module(uint8_t *buffer, const uint32_t size) {
    char error_buf[128];
    wasm_module_t module =
        wasm_runtime_load(buffer, size, error_buf, sizeof(error_buf));
    if (!module)
        ereport(ERROR, (errmsg("Error loading wasm module: %s", error_buf)));
    return module;
}

static inline uint8_t *
compile_aot(uint8_t *byte_code,
            const uint32_t size,
            uint32_t *out_aot_file_size) {
    AOTCompOption option = {
        .enable_gc = true,
        .aux_stack_frame_type = AOT_STACK_FRAME_TYPE_STANDARD,
        .call_stack_features = {
            .func_idx = true,
            .bounds_checks = true,
            .values = true,
        },
        .enable_tail_call = true,
        .enable_extended_const = true,
        .output_format = AOT_FORMAT_FILE,
        .enable_simd = true,
        .enable_aux_stack_check = true,
        .bounds_checks = true,
        .enable_bulk_memory = true,
    };
    wasm_module_t module = NULL;
    aot_comp_data_t comp_data = NULL;
    aot_comp_context_t comp_ctx = NULL;
    uint8_t *rv = NULL;

    PG_TRY();
    {
        module = load_wasm_module(byte_code, size);
        if (!(comp_data = aot_create_comp_data(module, NULL, true)))
            ereport(ERROR,
                    (errmsg("Error creating AOT compilation data: %s",
                            aot_get_last_error())));
        if (!(comp_ctx = aot_create_comp_context(comp_data, &option)))
            ereport(ERROR,
                    (errmsg("Error creating AOT compilation context: %s",
                            aot_get_last_error())));
        if (!aot_compile_wasm(comp_ctx))
            ereport(ERROR,
                    (errmsg("Error compiling wasm module: %s",
                            aot_get_last_error())));
        if (!(rv = aot_emit_aot_file_buf(comp_ctx,
                                         comp_data,
                                         out_aot_file_size)))
            ereport(
                ERROR,
                (errmsg("Error emitting AOT file: %s", aot_get_last_error())));
    }
    PG_FINALLY();
    {
        if (comp_ctx)
            aot_destroy_comp_context(comp_ctx);
        if (comp_data)
            aot_destroy_comp_data(comp_data);
        if (module)
            wasm_runtime_unload(module);
    }
    PG_END_TRY();
    return rv;
}

static void
run_start(wasm_exec_env_t exec_env, va_list args) {
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    wasm_function_inst_t start_func =
        wasm_runtime_lookup_function(module_inst, "_start");
    if (start_func)
        if (!wasm_runtime_call_wasm(exec_env, start_func, 0, NULL)) {
            const char *exc = wasm_runtime_get_exception(module_inst);
            StringInfoData msg;
            initStringInfo(&msg);
            enlargeStringInfo(&msg, 4096);
            msg.len += (int)wasm_runtime_dump_call_stack_to_buf(
                exec_env,
                msg.data + msg.len,
                msg.maxlen - msg.len - 1);
            while (msg.len >= 2 && msg.data[msg.len - 2] == '\n')
                msg.data[msg.len-- - 2] = '\0';
            ereport(ERROR, ({
                        errmsg("%s:\n%s", exc ? exc : "Error", msg.data);
                        pfree(msg.data);
                    }));
        }
}

static void
run_moontest(wasm_exec_env_t exec_env, va_list args) {
    const char *moontest_spec = va_arg(args, const char *);
    moontest_run(exec_env, moontest_spec);
}

static void
run_wasm_with(const char *wasm_file,
              bool use_aot,
              bool is_moontest,
              void (*fn)(wasm_exec_env_t, va_list),
              ...) {
    bool wamr_inited = false;
    uint8_t *buffer = NULL;
    uint32 size;
    wasm_module_t module = NULL;
    wasm_module_inst_t module_inst = NULL;
    wasm_exec_env_t exec_env = NULL;
    va_list args;

    PG_TRY();
    {
        init_wamr(is_moontest);
        wamr_inited = true;
        buffer = read_wasm_file(wasm_file, &size);
        if (use_aot) {
            uint8_t *aot_buffer = compile_aot(buffer, size, &size);
            pfree(buffer);
            buffer = aot_buffer;
        }
        module = load_wasm_module(buffer, size);
        {
            char error_buf[128];
            if (!(module_inst =
                      wasm_runtime_instantiate(module,
                                               STACK_K * 1024,
                                               (uint32_t)HEAP_M * 1024 * 1024,
                                               error_buf,
                                               sizeof(error_buf))))
                ereport(
                    ERROR,
                    (errmsg("Error instantiating wasm module: %s", error_buf)));
        }
        if (!(exec_env =
                  wasm_runtime_create_exec_env(module_inst, STACK_K * 1024)))
            ereport(ERROR, errmsg("Error creating execution environment"));

        va_start(args, fn);
        fn(exec_env, args);
        va_end(args);
    }
    PG_FINALLY();
    {
        if (exec_env)
            wasm_runtime_destroy_exec_env(exec_env);
        if (module_inst)
            wasm_runtime_deinstantiate(module_inst);
        if (module)
            wasm_runtime_unload(module);
        if (buffer)
            pfree(buffer);
        if (wamr_inited)
            wasm_runtime_destroy();
    }
    PG_END_TRY();
}

static void
init_locale(const char *categoryname, int category, const char *locale) {
    if (pg_perm_setlocale(category, locale) == NULL
        && pg_perm_setlocale(category, "C") == NULL)
        elog(FATAL,
             "could not adopt \"%s\" locale nor C locale for %s",
             locale,
             categoryname);
}
