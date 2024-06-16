#include "postgres.h"

PGDLLEXPORT void
rustica_worker(Datum index) {
    ereport(DEBUG1,
            (errmsg("worker rustica-%d starting", DatumGetInt32(index))));
}
