#include <bits/time.h>
#include <time.h>
#include <unistd.h>

#include "postgres.h"
#include "mb/pg_wchar.h"
#include "portability/instr_time.h"

#include "bh_log.h"

#include "rustica/env.h"

char **saved_argv;
int saved_argc;

static void
obj_finalizer(const wasm_obj_t obj, void *data) {
    rustica_value_t val =
        (rustica_value_t)wasm_anyref_obj_get_value((wasm_anyref_obj_t)obj);
    if (val->owns_ptr)
        pfree(val->ptr);
    pfree(val);
}

rustica_value_t
rustica_value_new(const uint8_t type, void *ptr, const size_t size) {
    const rustica_value_t rv =
        palloc(sizeof(RusticaValue)
               + (size > sizeof(void *) ? size - sizeof(void *) : 0));
    rv->type = type;
    rv->owns_ptr = false;
    if (size == 0) {
        rv->ptr = ptr;
        rv->data_builtin = false;
    }
    else {
        rv->data_builtin = true;
        if (ptr == NULL)
            memset(rv->data, 0, size);
        else
            memcpy(rv->data, ptr, size);
    }
    return rv;
}

wasm_externref_obj_t
rustica_value_to_wasm(wasm_exec_env_t exec_env, rustica_value_t val) {
    wasm_externref_obj_t rv = wasm_externref_obj_new(exec_env, val);
    wasm_obj_set_gc_finalizer(exec_env,
                              wasm_externref_obj_to_internal_obj(rv),
                              obj_finalizer,
                              exec_env);
    return rv;
}

rustica_value_t
rustica_value_from_wasm(wasm_obj_t refobj, uint8_t expected_type) {
    wasm_obj_t anyref =
        wasm_externref_obj_to_internal_obj((wasm_externref_obj_t)refobj);
    rustica_value_t rv =
        (rustica_value_t)wasm_anyref_obj_get_value((wasm_anyref_obj_t)anyref);
    return rv->type == expected_type ? rv : NULL;
}

static void
print_char(wasm_exec_env_t exec_env, int ch) {
    char buf[5];
    const int len = pg_wchar2mb_with_len((pg_wchar *)&ch, buf, 1);
    Assert(len < 5);
    buf[len] = 0;
    printf("%s", buf);
}

static NativeSymbol spectest_symbols[] = {
    { "print_char", print_char, "(i)", NULL },
};

static void
exception_throw(wasm_exec_env_t exec_env) {
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    wasm_runtime_set_exception(inst, "panic");
}

static NativeSymbol exception_symbols[] = {
    { "throw", exception_throw, "()", NULL },
};

static StringReader *
fs_begin_read_string(wasm_exec_env_t exec_env, wasm_obj_t ref) {
    rustica_value_t val = rustica_value_from_wasm(ref, RUSTICA_ENV_CSTRING);
    StringReader *buf = palloc(sizeof(StringReader));
    buf->offset = 0;
    if (val->data_builtin) {
        buf->size = strlen(val->data);
        buf->data = val->data;
    }
    else {
        buf->size = strlen(val->ptr);
        buf->data = val->ptr;
    }
    return buf;
}

static int32_t
fs_string_read_char(wasm_exec_env_t exec_env, StringReader *buf) {
    int ch;
    pg_wchar char_code[2] = { '?', 0 };
    if (buf->offset >= buf->size) {
        return -1; // EOF
    }
    // Read the next Unicode char code using Postgres C functions
    ch = pg_utf_mblen((unsigned char *)buf->data + buf->offset);
    if (ch > 0) {
        pg_encoding_mb2wchar_with_len(PG_UTF8,
                                      buf->data + buf->offset,
                                      char_code,
                                      ch);
        buf->offset += ch;
    }
    else {
        // Invalid UTF-8 sequence, skip one byte
        buf->offset += 1;
    }
    return (int32_t)char_code[0];
}

static void
fs_finish_read_string(wasm_exec_env_t exec_env, StringReader *buf) {
    pfree(buf);
}

static ArrayReader *
fs_begin_read_string_array(wasm_exec_env_t exec_env, wasm_obj_t ref) {
    rustica_value_t val =
        rustica_value_from_wasm(ref, RUSTICA_ENV_CSTRING_ARRAY);
    ArrayReader *reader = palloc(sizeof(ArrayReader));
    reader->offset = 0;
    if (val->data_builtin)
        reader->arr = val->arr;
    else
        reader->arr = val->ptr;
    return reader;
}

static wasm_externref_obj_t
fs_string_array_read_string(wasm_exec_env_t exec_env, ArrayReader *reader) {
    rustica_value_t rv;
    if (reader->offset >= reader->arr->size) {
        rv = rustica_value_new(RUSTICA_ENV_CSTRING,
                               "ffi_end_of_/string_array",
                               0);
    }
    else {
        rv = rustica_value_new(RUSTICA_ENV_CSTRING,
                               reader->arr->items[reader->offset],
                               0);
        reader->offset += 1;
    }
    return rustica_value_to_wasm(exec_env, rv);
}

static void
fs_finish_read_string_array(wasm_exec_env_t exec_env, ArrayReader *reader) {
    pfree(reader);
}

static wasm_externref_obj_t
fs_current_dir(wasm_exec_env_t exec_env) {
    char buf[1024];
    rustica_value_t rv;

    getcwd(buf, sizeof(buf));
    rv = rustica_value_new(RUSTICA_ENV_CSTRING, buf, strlen(buf) + 1);
    return rustica_value_to_wasm(exec_env, rv);
}

static wasm_externref_obj_t
fs_args_get(wasm_exec_env_t exec_env) {
    // return sys argv as rustica_value_t
    rustica_value_t rv = rustica_value_new(RUSTICA_ENV_CSTRING_ARRAY,
                                           NULL,
                                           sizeof(RusticaPointerArray)
                                               + sizeof(char *) * saved_argc);
    rv->arr->size = saved_argc;
    for (int i = 0; i < saved_argc; i++) {
        rv->arr->items[i] = saved_argv[i];
    }
    return rustica_value_to_wasm(exec_env, rv);
}

static NativeSymbol fs_symbols[] = {
    { "begin_read_string", fs_begin_read_string, "(r)r", NULL },
    { "string_read_char", fs_string_read_char, "(r)i", NULL },
    { "finish_read_string", fs_finish_read_string, "(r)", NULL },
    { "begin_read_string_array", fs_begin_read_string_array, "(r)r", NULL },
    { "string_array_read_string", fs_string_array_read_string, "(r)r", NULL },
    { "finish_read_string_array", fs_finish_read_string_array, "(r)", NULL },
    { "current_dir", fs_current_dir, "()r", NULL },
    { "args_get", fs_args_get, "()r", NULL },
};

static uint64_t
time_now(wasm_exec_env_t exec_env) {
    // return current time in milliseconds since epoch
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static wasm_externref_obj_t
time_instant_now(wasm_exec_env_t exec_env) {
    rustica_value_t rv =
        rustica_value_new(RUSTICA_ENV_INSTR_TIME, NULL, sizeof(instr_time));
    INSTR_TIME_SET_CURRENT(*rv->instr_time);
    return rustica_value_to_wasm(exec_env, rv);
}

static double
time_instant_elapsed_as_secs_f64(wasm_exec_env_t exec_env, wasm_obj_t ref) {
    rustica_value_t val = rustica_value_from_wasm(ref, RUSTICA_ENV_INSTR_TIME);
    instr_time it;
    INSTR_TIME_SET_CURRENT(it);
    INSTR_TIME_SUBTRACT(it, *val->instr_time);
    return INSTR_TIME_GET_DOUBLE(it);
}

static NativeSymbol time_symbols[] = {
    { "now", time_now, "()I", NULL },
    { "instant_now", time_instant_now, "()r", NULL },
    { "instant_elapsed_as_secs_f64",
      time_instant_elapsed_as_secs_f64,
      "(r)F",
      NULL },
};

void
rustica_register_natives() {
    if (!wasm_runtime_register_natives("spectest",
                                       spectest_symbols,
                                       sizeof(spectest_symbols)
                                           / sizeof(NativeSymbol)))
        ereport(ERROR, errmsg("Failed to register spectest natives"));
    if (!wasm_runtime_register_natives("exception",
                                       exception_symbols,
                                       sizeof(exception_symbols)
                                           / sizeof(NativeSymbol)))
        ereport(ERROR, errmsg("Failed to register exception natives"));
    if (!wasm_runtime_register_natives("__moonbit_fs_unstable",
                                       fs_symbols,
                                       sizeof(fs_symbols)
                                           / sizeof(NativeSymbol)))
        ereport(ERROR, errmsg("Failed to register fs natives"));
    if (!wasm_runtime_register_natives("__moonbit_time_unstable",
                                       time_symbols,
                                       sizeof(time_symbols)
                                           / sizeof(NativeSymbol)))
        ereport(ERROR, errmsg("Failed to register time natives"));
}

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
pg_bh_log(LogLevel log_level, const char *file, int line, const char *fmt, ...) {
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
    } while(0);
}
