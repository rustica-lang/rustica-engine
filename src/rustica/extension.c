#include "postgres.h"
#include "utils/builtins.h"

PG_MODULE_MAGIC;

void
_PG_init(void) {
    ereport(LOG, errmsg("Rustica Engine is now loaded"));
}
