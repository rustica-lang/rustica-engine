#include "postgres.h"
#include "getopt_long.h"
#include "utils/memutils.h"
#include "utils/pg_locale.h"

#include "bh_platform.h"
#include "bh_read_file.h"
#include "gc_export.h"

#include "rustica/env.h"
#include "rustica/moontest.h"

#define HEAP_M 512
#define STACK_K 512

const char *progname;

static void
run_start(wasm_exec_env_t exec_env, va_list args);

static void
run_moontest(wasm_exec_env_t exec_env, va_list args);

static void
run_wasm_with(const char *wasm_file, void (*fn)(wasm_exec_env_t, va_list), ...);

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
    printf(_("  -h, --help     show this help, then exit\n"));
    printf(_("  -V, --version  output version information, then exit\n"));
}

static Subcommand
parse_args(int argc, char **argv) {
    struct option long_options[] = { { "help", no_argument, NULL, 'h' },
                                     { "version", no_argument, NULL, 'V' },
                                     { NULL, 0, NULL, 0 } };

    int c;
    while ((c = getopt_long(argc, argv, "+m:hV", long_options, NULL)) != -1) {
        switch (c) {
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
    int rv = 0;

    saved_argc = argc;
    saved_argv = argv;
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

    PG_TRY();
    {
        Subcommand subcmd = parse_args(argc, argv);
        switch (subcmd) {
            case RUN:
                if (optind >= argc)
                    ereport(ERROR, errmsg("No wasm file specified for 'run'"));
                run_wasm_with(argv[optind], run_start);
                break;

            case MOONTEST:
            {
                const char *moontest_spec, *wasm_file;
                if (moontest_parse_args(argc,
                                        argv,
                                        &moontest_spec,
                                        &wasm_file)) {
                    if (wasm_file)
                        run_wasm_with(wasm_file, run_moontest, moontest_spec);
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

static inline void
init_wamr() {
    RuntimeInitArgs init_args = {
        .mem_alloc_type = Alloc_With_Allocator,
        .mem_alloc_option = { .allocator = { .malloc_func = palloc,
                                             .realloc_func = repalloc,
                                             .free_func = pfree } },
        .gc_heap_size = (uint32_t)HEAP_M * 1024 * 1024,
        .running_mode = Mode_Interp,
    };

    if (!wasm_runtime_full_init(&init_args))
        ereport(ERROR, errmsg("Failed to initialize WASM runtime"));

    rustica_register_natives();
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
        init_wamr();
        wamr_inited = true;
        buffer = read_wasm_file(wasm_file, &size);
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
