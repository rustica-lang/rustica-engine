// SPDX-FileCopyrightText: 2025 燕几（北京）科技有限公司
// SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0

#include "postgres.h"

#include "bh_log.h"

int
pg_log_vprintf(const char *format, va_list ap) {
    int rv = 0;
    ereport_domain(LOG, "WAMR", ({
                       StringInfoData buf;
                       initStringInfo(&buf);
                       for (;;) {
                           const int needed =
                               appendStringInfoVA(&buf, format, ap);
                           if (needed == 0)
                               break;
                           enlargeStringInfo(&buf, needed);
                       }
                       while (buf.len > 0 && buf.data[buf.len - 1] == '\n') {
                           buf.data[buf.len - 1] = '\0';
                           buf.len -= 1;
                       }
                       rv = buf.len;
                       errmsg_internal(buf.data);
                       pfree(buf.data);
                   }));
    return rv;
}

void
pg_bh_log(LogLevel log_level,
          const char *file,
          int line,
          const char *fmt,
          ...) {
    int elevel = LOG;
    switch (log_level) {
        case BH_LOG_LEVEL_FATAL:
            elevel = FATAL;
            break;
        case BH_LOG_LEVEL_ERROR:
            elevel = ERROR;
            break;
        case BH_LOG_LEVEL_WARNING:
            elevel = WARNING;
            break;
        case BH_LOG_LEVEL_DEBUG:
            elevel = DEBUG1;
            break;
        case BH_LOG_LEVEL_VERBOSE:
            elevel = DEBUG3;
            break;
    }
    do {
        pg_prevent_errno_in_scope();
        if (errstart(elevel, "WAMR")) {
            StringInfoData buf;
            initStringInfo(&buf);
            for (;;) {
                va_list ap;
                int needed;
                va_start(ap, fmt);
                needed = appendStringInfoVA(&buf, fmt, ap);
                va_end(ap);
                if (needed == 0)
                    break;
                enlargeStringInfo(&buf, needed);
            }
            errmsg_internal(buf.data);
            pfree(buf.data);
            errfinish(file, line, "-");
        }
        if (elevel >= ERROR)
            pg_unreachable();
    } while (0);
}
