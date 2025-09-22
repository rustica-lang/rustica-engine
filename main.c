#include <wchar.h>

#include "postgres.h"
#include "getopt_long.h"
#include "utils/memutils.h"
#include "utils/pg_locale.h"
#include "bh_platform.h"
#include "bh_read_file.h"
#include "wasm_export.h"

const char *progname;


static void init_locale(const char *categoryname, int category, const char *locale);

static void
help()
{
	printf(_("%s is a WebAssembly runtime with PostgreSQL backend.\n\n"), progname);
	printf(_("Usage:\n"));
	printf(_("  %s [OPTION]... <wasm_file>\n\n"), progname);
	printf(_("Options:\n"));
	printf(_("  -h, --help     show this help, then exit\n"));
	printf(_("  -V, --version  output version information, then exit\n"));
}

static void
print_char(wasm_exec_env_t exec_env, int ch) {
	wprintf(L"%lc", ch);
}

static NativeSymbol native_symbols[] = {
	{ "print_char", print_char, "(i)", NULL },
};

int
main(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'V'},
		{NULL, 0, NULL, 0}
	};

	int			c;
	char error_buf[128];
	uint32 size;
	uint8_t *buffer;

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

	// 先处理命令行选项
	while ((c = getopt_long(argc, argv, "hV", long_options, NULL)) != -1)
	{
		switch (c)
		{
			case 'h':
				help();
				exit(0);
			case 'V':
				printf("%s (PostgreSQL %s)\n", progname, PG_VERSION);
				exit(0);
			default:
				fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
				exit(1);
		}
	}

	// 检查是否提供了wasm文件路径
	if (optind >= argc) {
		fprintf(stderr, "Error: No wasm file specified\n");
		fprintf(stderr, "Usage: %s [OPTION]... <wasm_file>\n", progname);
		fprintf(stderr, "Try '%s --help' for more information.\n", progname);
		return 1;
	}

	// 初始化WASM运行时并加载文件
	wasm_runtime_init();
	buffer = (uint8_t *)bh_read_file_to_buffer(argv[optind], &size);
	if (!buffer) {
		fprintf(stderr, "Error: Failed to read file: %s\n", argv[optind]);
		return 1;
	}

	wasm_runtime_register_natives("spectest", native_symbols, 1);
	wasm_module_t module = wasm_runtime_load(buffer, size, error_buf, sizeof(error_buf));
	if (!module) {
		fprintf(stderr, "Error loading wasm module: %s\n", error_buf);
		return 1;
	}

	wasm_module_inst_t module_inst = wasm_runtime_instantiate(module, 32768, 32768,
									   error_buf, sizeof(error_buf));
	if (!module_inst) {
		fprintf(stderr, "Error instantiating wasm module: %s\n", error_buf);
		return 1;
	}

	wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(module_inst, 32768);
	if (!exec_env) {
		fprintf(stderr, "Error creating execution environment\n");
		return 1;
	}

	wasm_function_inst_t start_func = wasm_runtime_lookup_function(module_inst, "_start");
	if (!start_func) {
		fprintf(stderr, "Error: _start function not found\n");
		return 1;
	}

	wasm_runtime_call_wasm(exec_env, start_func, 0, NULL);
	const char *exc = wasm_runtime_get_exception(module_inst);
	if (exc)
		printf("Exception: %s\n", exc);

	// 清理资源
	wasm_runtime_destroy_exec_env(exec_env);
	wasm_runtime_deinstantiate(module_inst);
	wasm_runtime_unload(module);
	wasm_runtime_destroy();

	return 0;
}

static void
init_locale(const char *categoryname, int category, const char *locale)
{
	if (pg_perm_setlocale(category, locale) == NULL &&
		pg_perm_setlocale(category, "C") == NULL)
		elog(FATAL, "could not adopt \"%s\" locale nor C locale for %s",
			 locale, categoryname);
}
