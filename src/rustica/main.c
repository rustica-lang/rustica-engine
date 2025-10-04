#include "postgres.h"
#include "getopt_long.h"
#include "utils/memutils.h"
#include "utils/pg_locale.h"

#include "bh_platform.h"
#include "bh_read_file.h"
#include "gc_export.h"
#include "wasm_exec_env.h"

#include "rustica/env.h"
#include "rustica/moontest.h"

#define HEAP_M 512
#define STACK_K 512

const char *progname;

static void
init_locale(const char *categoryname, int category, const char *locale);

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

int
main(int argc, char *argv[]) {
    static struct option long_options[] = {
        { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 }
    };

    int c;
    int subcmd_index = 1;
    char error_buf[128];
    uint32 size;
    uint8_t *buffer;
    const char *moontest_spec = NULL;
    const char *wasm_file = NULL;
    const char *dot = NULL;
    RuntimeInitArgs init_args = {
        .mem_alloc_type = Alloc_With_Allocator,
        .mem_alloc_option = { .allocator = { .malloc_func = palloc,
                                             .realloc_func = repalloc,
                                             .free_func = pfree } },
        .gc_heap_size = (uint32_t)HEAP_M * 1024 * 1024,
        .running_mode = Mode_Interp,
    };
    wasm_module_t module = NULL;
    wasm_module_inst_t module_inst = NULL;
    wasm_exec_env_t exec_env = NULL;
    const char *exc = NULL;

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

    /* If no args, show help */
    if (argc < 2) {
        help();
        exit(0);
    }

    /* If the first non-program argument begins with '-', parse global options
   (e.g. prog -h or prog --version). Otherwise treat argv[1] as subcommand. */
    if (argv[1][0] == '-') {
        optind = 1;
        while ((c = getopt_long(argc, argv, "hV", long_options, NULL)) != -1) {
            switch (c) {
                case 'h':
                    help();
                    exit(0);
                case 'V':
                    printf("%s (PostgreSQL %s)\n", progname, PG_VERSION);
                    exit(0);
                default:
                    fprintf(stderr,
                            _("Try \"%s --help\" for more information.\n"),
                            progname);
                    exit(1);
            }
        }
        if (optind >= argc) {
            help();
            exit(0);
        }
        subcmd_index = optind;
    }

    /* Support minimal subcommands:
       - 'run <wasm_file>' (no options)
       - 'moontest --spec <JSON> <wasm_file>' (parse options starting after
       subcommand)
    */
    if (strcmp(argv[subcmd_index], "run") == 0) {
        if (subcmd_index + 1 >= argc) {
            fprintf(stderr, "Error: No wasm file specified for 'run'\n");
            return 1;
        }
        wasm_file = argv[subcmd_index + 1];
    }
    else if (strcmp(argv[subcmd_index], "moontest") == 0) {
        optind = subcmd_index + 1; /* start parsing after subcommand */
        if (moontest_parse_args(argc, argv, &moontest_spec, &wasm_file) != 0) {
            return 1;
        }
        if (moontest_spec == NULL) {
            // Help was already printed by moontest_parse_args
            return 0;
        }
        Assert(wasm_file != NULL);
    }
    else {
        /* Unknown subcommand -> show help */
        fprintf(stderr, "Unsupported command '%s'\n", argv[subcmd_index]);
        help();
        return 1;
    }

    // 检查文件扩展名是否为 .wasm
    dot = strrchr(wasm_file, '.');
    if (!dot || strcmp(dot, ".wasm") != 0) {
        fprintf(stderr, "Unsupported file type\n");
        return 1;
    }

    // 初始化WASM运行时并加载文件
    if (!wasm_runtime_full_init(&init_args)) {
        fprintf(stderr, "Error: Failed to initialize WASM runtime\n");
        return 1;
    }
    buffer = (uint8_t *)bh_read_file_to_buffer(wasm_file, &size);
    if (!buffer) {
        fprintf(stderr, "Error: Failed to read file: %s\n", argv[optind]);
        return 1;
    }

    rustica_register_natives();

    module = wasm_runtime_load(buffer, size, error_buf, sizeof(error_buf));
    if (!module) {
        fprintf(stderr, "Error loading wasm module: %s\n", error_buf);
        return 1;
    }

    module_inst = wasm_runtime_instantiate(module,
                                           STACK_K * 1024,
                                           (uint32_t)HEAP_M * 1024 * 1024,
                                           error_buf,
                                           sizeof(error_buf));
    if (!module_inst) {
        fprintf(stderr, "Error instantiating wasm module: %s\n", error_buf);
        return 1;
    }

    exec_env = wasm_runtime_create_exec_env(module_inst, STACK_K * 1024);
    if (!exec_env) {
        fprintf(stderr, "Error creating execution environment\n");
        return 1;
    }
    exec_env->disable_dump_call_stack = true;

    PG_TRY();
    {
        if (moontest_spec) {
            moontest_run(exec_env, moontest_spec);
        }
        else {
            wasm_function_inst_t start_func =
                wasm_runtime_lookup_function(module_inst, "_start");
            if (start_func)
                wasm_runtime_call_wasm(exec_env, start_func, 0, NULL);
        }
    }
    PG_CATCH();
    {
        ErrorData *edata = CopyErrorData();
        fprintf(stderr, "Error: %s\n", edata->message);
        FlushErrorState();
        FreeErrorData(edata);
    }
    PG_END_TRY();

    exc = wasm_runtime_get_exception(module_inst);
    if (exc) {
        printf("Exception: %s\n", exc);
        wasm_runtime_dump_call_stack(exec_env);
    }

    wasm_runtime_destroy_exec_env(exec_env);
    wasm_runtime_deinstantiate(module_inst);
    wasm_runtime_unload(module);
    wasm_runtime_destroy();

    return 0;
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
