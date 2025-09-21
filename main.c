#include "postgres.h"
#include "getopt_long.h"
#include "utils/memutils.h"
#include "utils/pg_locale.h"

const char *progname;


static void init_locale(const char *categoryname, int category, const char *locale);

static void
help(const char *progname)
{
	printf(_("%s is a custom PostgreSQL CLI tool.\n\n"), progname);
	printf(_("Usage:\n"));
	printf(_("  %s [OPTION]...\n\n"), progname);
	printf(_("Options:\n"));
	printf(_("  -h, --help     show this help, then exit\n"));
	printf(_("  -V, --version  output version information, then exit\n"));
}

int
main(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'V'},
		{NULL, 0, NULL, 0}
	};

	int			c;

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

	while ((c = getopt_long(argc, argv, "hV", long_options, NULL)) != -1)
	{
		switch (c)
		{
			case 'h':
				help(progname);
				exit(0);
			case 'V':
				puts("rustica_cli (PostgreSQL) " PG_VERSION);
				exit(0);
			default:
				fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
				exit(1);
		}
	}

	printf("Hello from Rustica CLI!\n");
	printf("This is a custom PostgreSQL-based CLI tool.\n");
	printf("You can add your custom logic here.\n");

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
