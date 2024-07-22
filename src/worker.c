#include <sys/socket.h>
#include <sys/un.h>
#include <wchar.h>

#include "postgres.h"
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "libpq/libpq.h"
#include "libpq/pqformat.h"
#include "access/xact.h"
#include "commands/async.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/snapmgr.h"
#include "pgstat.h"
#include "tcop/utility.h"
#include "wasm_runtime_common.h"
#include "wasm_memory.h"
#include "aot_runtime.h"
#include "llhttp.h"

#include "rustica_wamr.h"

#define WAIT_WRITE 0
#define WAIT_READ 0
static int worker_id;
static pgsocket sock;
static char hello[12];
static WaitEventSet *wait_set = NULL;
static bool shutdown_requested = false;
static bool notify_received = false;
static char state = WAIT_WRITE;
static int sent = 0;
static FDMessage fd_msg;
static SPIPlanPtr load_module_plan = NULL;
static const char *load_module_sql =
    "SELECT bin_code FROM rustica.modules WHERE name = $1";

void
spectest_print_char(wasm_exec_env_t exec_env, int c) {
    fwprintf(stderr, L"%lc", c);
}

static NativeSymbol spectest[] = {
    { "print_char", spectest_print_char, "(i)" }
};

static int32_t
env_recv(wasm_exec_env_t exec_env,
         WASMArrayObjectRef buf,
         int32_t start,
         int32_t len) {
    WaitEvent events[1];
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    char *view = wasm_array_obj_first_elem_addr(buf);
    ModifyWaitEvent(ctx->wait_set,
                    1,
                    WL_SOCKET_READABLE | WL_SOCKET_CLOSED,
                    NULL);
    WaitEventSetWait(ctx->wait_set, -1, events, 1, WAIT_EVENT_CLIENT_READ);
    if (events[0].events & WL_LATCH_SET) {
        return -1;
    }
    else if (events[0].events & WL_SOCKET_CLOSED) {
        return 0;
    }
    else {
        return recv(ctx->fd, view + start, len, 0);
    }
}

static int32_t
env_send(wasm_exec_env_t exec_env,
         WASMArrayObjectRef buf,
         int32_t start,
         int32_t len) {
    WaitEvent events[1];
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    char *view = wasm_array_obj_first_elem_addr(buf);
    ModifyWaitEvent(ctx->wait_set,
                    1,
                    WL_SOCKET_WRITEABLE | WL_SOCKET_CLOSED,
                    NULL);
    WaitEventSetWait(ctx->wait_set, -1, events, 1, WAIT_EVENT_CLIENT_WRITE);
    if (events[0].events & WL_LATCH_SET) {
        return -1;
    }
    else if (events[0].events & WL_SOCKET_CLOSED) {
        return 0;
    }
    else {
        return send(ctx->fd, view + start, len, 0);
    }
}

static void
maybe_call_on_error(wasm_exec_env_t exec_env, llhttp_errno_t rv) {
    if (rv == HPE_OK || rv == HPE_PAUSED)
        return;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    if (!ctx->on_error)
        return;
    if (ctx->bytes == -1) {
        wasm_func_type_t func_type =
            wasm_runtime_get_function_type(ctx->on_error,
                                           exec_env->module_inst->module_type);
        ctx->bytes = wasm_func_type_get_param_type(func_type, 0).heap_type;
    }
    char *reason = llhttp_get_error_reason(&ctx->http_parser);
    uint32_t size = strlen(reason);
    WASMArrayObjectRef buf =
        wasm_array_obj_new_with_typeidx(exec_env, ctx->bytes, size, NULL);
    char *view = wasm_array_obj_first_elem_addr(buf);
    memcpy(view, reason, size);
    wasm_val_t args[1] = { { .kind = WASM_EXTERNREF, .of.foreign = buf } };
    wasm_val_t results[1];
    if (!wasm_runtime_call_wasm_a(exec_env,
                                  ctx->on_error,
                                  1,
                                  results,
                                  1,
                                  args)) {
        ereport(WARNING, errmsg("failed to run WASM function on_error"));
    }
}

static int32_t
env_llhttp_execute(wasm_exec_env_t exec_env,
                   WASMArrayObjectRef buf,
                   int32_t start,
                   int32_t len) {
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    char *view = wasm_array_obj_first_elem_addr(buf);
    llhttp_errno_t rv;
    ctx->current_buf = buf;
    rv = llhttp_execute(&ctx->http_parser, view + start, len);
    ctx->current_buf = NULL;
    maybe_call_on_error(exec_env, rv);
    return rv;
}

static int32_t
env_llhttp_resume(wasm_exec_env_t exec_env) {
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    llhttp_resume(&ctx->http_parser);
    return HPE_OK;
}

static int32_t
env_llhttp_finish(wasm_exec_env_t exec_env, WASMArrayObjectRef buf) {
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    llhttp_errno_t rv;
    ctx->current_buf = buf;
    rv = llhttp_finish(&ctx->http_parser);
    ctx->current_buf = NULL;
    maybe_call_on_error(exec_env, rv);
    return rv;
}

static int32_t
env_llhttp_reset(wasm_exec_env_t exec_env) {
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    llhttp_reset(&ctx->http_parser);
    return HPE_OK;
}

static int32_t
env_llhttp_get_error_pos(wasm_exec_env_t exec_env, WASMArrayObjectRef buf) {
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    char *view = wasm_array_obj_first_elem_addr(buf);
    return (int32_t)(llhttp_get_error_pos(&ctx->http_parser) - view);
}

static int32_t
env_llhttp_get_method(wasm_exec_env_t exec_env) {
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_get_method(&ctx->http_parser);
}

static int32_t
env_llhttp_get_http_major(wasm_exec_env_t exec_env) {
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_get_http_major(&ctx->http_parser);
}

static int32_t
env_llhttp_get_http_minor(wasm_exec_env_t exec_env) {
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_get_http_minor(&ctx->http_parser);
}

static NativeSymbol native_env[] = {
    { "recv", env_recv, "(rii)i" },
    { "send", env_send, "(rii)i" },
    { "llhttp_execute", env_llhttp_execute, "(rii)i" },
    { "llhttp_resume", env_llhttp_resume, "()i" },
    { "llhttp_finish", env_llhttp_finish, "(r)i" },
    { "llhttp_reset", env_llhttp_reset, "()i" },
    { "llhttp_get_error_pos", env_llhttp_get_error_pos, "(r)i" },
    { "llhttp_get_method", env_llhttp_get_method, "()i" },
    { "llhttp_get_http_major", env_llhttp_get_http_major, "()i" },
    { "llhttp_get_http_minor", env_llhttp_get_http_minor, "()i" },
    { "prepare_statement", env_prepare_statement, "(rr)i" },
    { "execute_statement", env_execute_statement, "(i)i" },
    { "detoast", env_detoast, "(iii)r" },
};

static int
llhttp_data_cb_impl(wasm_exec_env_t exec_env,
                    wasm_function_inst_t func,
                    const char *at,
                    size_t length) {
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    if (ctx->bytes_view == -1) {
        wasm_func_type_t func_type =
            wasm_runtime_get_function_type(func,
                                           exec_env->module_inst->module_type);
        ctx->bytes_view = wasm_func_type_get_param_type(func_type, 0).heap_type;
    }
    WASMStructObjectRef view =
        wasm_struct_obj_new_with_typeidx(exec_env, ctx->bytes_view);
    wasm_value_t buf_ref, start, len;
    buf_ref.gc_obj = ctx->current_buf;
    start.i32 =
        (int32_t)(at
                  - (char *)wasm_array_obj_first_elem_addr(ctx->current_buf));
    len.i32 = (int32_t)length;
    wasm_struct_obj_set_field(view, 0, &buf_ref);
    wasm_struct_obj_set_field(view, 1, &start);
    wasm_struct_obj_set_field(view, 2, &len);
    wasm_val_t results[1];
    wasm_val_t args[1] = {
        { .kind = WASM_EXTERNREF, .of.foreign = view },
    };
    if (!wasm_runtime_call_wasm_a(exec_env, func, 1, results, 1, args)) {
        return -1;
    }
    switch (results[0].of.i32) {
        case 0:
            return HPE_OK;
        case 1:
            return -1;
        case 2:
            return HPE_PAUSED;
    }
    return -1;
}

static int
llhttp_cb_impl(wasm_exec_env_t exec_env, wasm_function_inst_t func) {
    wasm_val_t results[1];
    if (!wasm_runtime_call_wasm_a(exec_env, func, 1, results, 0, NULL)) {
        return -1;
    }
    switch (results[0].of.i32) {
        case 0:
            return HPE_OK;
        case 1:
            return -1;
        case 2:
            return HPE_PAUSED;
    }
    return -1;
}

static int
on_message_begin(llhttp_t *p) {
    wasm_exec_env_t exec_env = p->data;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_cb_impl(exec_env, ctx->on_message_begin);
}

static int
on_method(llhttp_t *p, const char *at, size_t length) {
    wasm_exec_env_t exec_env = p->data;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_data_cb_impl(exec_env, ctx->on_method, at, length);
}

static int
on_method_complete(llhttp_t *p) {
    wasm_exec_env_t exec_env = p->data;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_cb_impl(exec_env, ctx->on_method_complete);
}

static int
on_url(llhttp_t *p, const char *at, size_t length) {
    wasm_exec_env_t exec_env = p->data;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_data_cb_impl(exec_env, ctx->on_url, at, length);
}

static int
on_url_complete(llhttp_t *p) {
    wasm_exec_env_t exec_env = p->data;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_cb_impl(exec_env, ctx->on_url_complete);
}

static int
on_version(llhttp_t *p, const char *at, size_t length) {
    wasm_exec_env_t exec_env = p->data;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_data_cb_impl(exec_env, ctx->on_version, at, length);
}

static int
on_version_complete(llhttp_t *p) {
    wasm_exec_env_t exec_env = p->data;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_cb_impl(exec_env, ctx->on_version_complete);
}

static int
on_header_field(llhttp_t *p, const char *at, size_t length) {
    wasm_exec_env_t exec_env = p->data;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_data_cb_impl(exec_env, ctx->on_header_field, at, length);
}

static int
on_header_field_complete(llhttp_t *p) {
    wasm_exec_env_t exec_env = p->data;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_cb_impl(exec_env, ctx->on_header_field_complete);
}

static int
on_header_value(llhttp_t *p, const char *at, size_t length) {
    wasm_exec_env_t exec_env = p->data;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_data_cb_impl(exec_env, ctx->on_header_value, at, length);
}

static int
on_headers_complete(llhttp_t *p) {
    wasm_exec_env_t exec_env = p->data;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_cb_impl(exec_env, ctx->on_headers_complete);
}

static int
on_header_value_complete(llhttp_t *p) {
    wasm_exec_env_t exec_env = p->data;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_cb_impl(exec_env, ctx->on_header_value_complete);
}

static int
on_body(llhttp_t *p, const char *at, size_t length) {
    wasm_exec_env_t exec_env = p->data;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_data_cb_impl(exec_env, ctx->on_body, at, length);
}

static int
on_message_complete(llhttp_t *p) {
    wasm_exec_env_t exec_env = p->data;
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    return llhttp_cb_impl(exec_env, ctx->on_message_complete);
}

static bool
wasm_module_reader_callback(package_type_t module_type,
                            const char *module_name,
                            uint8 **p_buffer,
                            uint32 *p_size) {
    Datum name_datum;
    int ret;
    bool isnull;
    bytea *bin_code;

    ereport(LOG,
            (errmsg("wasm_module_reader_callback: load WASM dependency \"%s\"",
                    module_name)));
    pgstat_report_activity(STATE_RUNNING, "loading WASM dependency");

    name_datum = CStringGetTextDatum(module_name);
    debug_query_string = load_module_sql;
    ret = SPI_execute_plan(load_module_plan, &name_datum, NULL, true, 1);

    if (ret != SPI_OK_SELECT) {
        ereport(ERROR, (errmsg("SPI_execute_plan failed: error code %d", ret)));
        pg_unreachable();
    }

    if (SPI_processed == 0) {
        ereport(ERROR, (errmsg("Dependency '%s' not found", module_name)));
        pg_unreachable();
    }

    bin_code = DatumGetByteaPP(SPI_getbinval(SPI_tuptable->vals[0],
                                             SPI_tuptable->tupdesc,
                                             1,
                                             &isnull));
    debug_query_string = NULL;
    Assert(!isnull);

    *p_size = VARSIZE_ANY_EXHDR(bin_code);
    *p_buffer = (uint8 *)VARDATA_ANY(bin_code);
    Assert(*p_buffer != NULL);
    return true;
}

static void
wasm_module_destroyer_callback(uint8 *buffer, uint32 size) {}

static void
startup() {
    struct sockaddr_un addr;

    MemAllocOption mem_option = { 0 };
    mem_option.allocator.malloc_func = palloc;
    mem_option.allocator.realloc_func = repalloc;
    mem_option.allocator.free_func = pfree;
    wasm_runtime_memory_init(Alloc_With_Allocator, &mem_option);

    memset(&fd_msg, 0, sizeof(FDMessage));
    fd_msg.io.iov_base = &fd_msg.byte;
    fd_msg.io.iov_len = 1;
    fd_msg.msg.msg_iov = &fd_msg.io;
    fd_msg.msg.msg_iovlen = 1;
    fd_msg.msg.msg_control = fd_msg.buf;
    fd_msg.msg.msg_controllen = sizeof(fd_msg.buf);
    fd_msg.cmsg = CMSG_FIRSTHDR(&fd_msg.msg);

    wait_set = CreateWaitEventSet(CurrentMemoryContext, 2);
    AddWaitEventToSet(wait_set, WL_LATCH_SET, PGINVALID_SOCKET, MyLatch, NULL);

    snprintf(hello, 12, BACKEND_HELLO);
    *((int *)&hello[8]) = worker_id;

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == PGINVALID_SOCKET)
        ereport(FATAL,
                (errmsg("rustica-%d: could not create Unix socket: %m",
                        worker_id)));
    make_ipc_addr(&addr);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) < 0)
        ereport(FATAL,
                (errmsg("rustica-%d: could not connect Unix socket: %m",
                        worker_id)));
    AddWaitEventToSet(wait_set,
                      WL_SOCKET_WRITEABLE | WL_SOCKET_CLOSED,
                      sock,
                      NULL,
                      NULL);
    if (rst_database != NULL) {
        BackgroundWorkerInitializeConnection(rst_database, NULL, 0);

        SetCurrentStatementStartTimestamp();
        StartTransactionCommand();
        SPI_connect();

        Async_Listen("rustica_module_cache_invalidation");

        debug_query_string = load_module_sql;
        load_module_plan = SPI_prepare(load_module_sql, 1, (Oid[1]){ TEXTOID });

        if (!load_module_plan) {
            ereport(ERROR,
                    (errmsg("could not prepare SPI plan: %s",
                            SPI_result_code_string(SPI_result))));
        }

        if (SPI_keepplan(load_module_plan)) {
            ereport(ERROR, (errmsg("failed to keep plan")));
        }

        debug_query_string = NULL;
        SPI_finish();
        CommitTransactionCommand();
    }
    if (!wasm_runtime_register_natives("spectest", spectest, 1))
        ereport(FATAL,
                (errmsg("rustica-%d: could not register natives", worker_id)));
    if (!wasm_runtime_register_natives("env",
                                       native_env,
                                       sizeof(native_env)
                                           / sizeof(native_env[0])))
        ereport(FATAL,
                (errmsg("rustica-%d: could not register natives", worker_id)));
    wasm_runtime_set_module_reader(wasm_module_reader_callback,
                                   wasm_module_destroyer_callback);
}

static inline void
on_writeable() {
    ssize_t nbytes;

    nbytes = send(sock, hello + sent, 12 - sent, 0);
    if (nbytes < 0) {
        ereport(DEBUG1,
                (errmsg("rustica-%d: could not send over Unix socket: %m",
                        worker_id)));
        return;
    }
    sent += (int)nbytes;
    if (sent == 12) {
        ereport(DEBUG1,
                (errmsg("rustica-%d: idle message sent, wait for jobs",
                        worker_id)));
        state = WAIT_READ;
        sent = 0;
        ModifyWaitEvent(wait_set,
                        1,
                        WL_SOCKET_READABLE | WL_SOCKET_CLOSED,
                        NULL);
    }
}

static inline void
init_llhttp(Context *ctx, wasm_module_inst_t instance) {
    wasm_function_inst_t func;

    llhttp_init(&ctx->http_parser, HTTP_REQUEST, &ctx->http_settings);
    ctx->bytes = -1;
    ctx->bytes_view = -1;

    if ((func = wasm_runtime_lookup_function(instance, "on_message_begin"))) {
        ctx->on_message_begin = func;
        ctx->http_settings.on_message_begin = on_message_begin;
    }

    if ((func = wasm_runtime_lookup_function(instance, "on_method"))) {
        ctx->on_method = func;
        ctx->http_settings.on_method = on_method;
    }

    if ((func = wasm_runtime_lookup_function(instance, "on_method_complete"))) {
        ctx->on_method_complete = func;
        ctx->http_settings.on_method_complete = on_method_complete;
    }

    if ((func = wasm_runtime_lookup_function(instance, "on_url"))) {
        ctx->on_url = func;
        ctx->http_settings.on_url = on_url;
    }

    if ((func = wasm_runtime_lookup_function(instance, "on_url_complete"))) {
        ctx->on_url_complete = func;
        ctx->http_settings.on_url_complete = on_url_complete;
    }

    if ((func = wasm_runtime_lookup_function(instance, "on_version"))) {
        ctx->on_version = func;
        ctx->http_settings.on_version = on_version;
    }

    if ((func =
             wasm_runtime_lookup_function(instance, "on_version_complete"))) {
        ctx->on_version_complete = func;
        ctx->http_settings.on_version_complete = on_version_complete;
    }

    if ((func = wasm_runtime_lookup_function(instance, "on_header_field"))) {
        ctx->on_header_field = func;
        ctx->http_settings.on_header_field = on_header_field;
    }

    if ((func = wasm_runtime_lookup_function(instance,
                                             "on_header_field_complete"))) {
        ctx->on_header_field_complete = func;
        ctx->http_settings.on_header_field_complete = on_header_field_complete;
    }

    if ((func = wasm_runtime_lookup_function(instance, "on_header_value"))) {
        ctx->on_header_value = func;
        ctx->http_settings.on_header_value = on_header_value;
    }

    if ((func = wasm_runtime_lookup_function(instance,
                                             "on_header_value_complete"))) {
        ctx->on_header_value_complete = func;
        ctx->http_settings.on_header_value_complete = on_header_value_complete;
    }

    if ((func =
             wasm_runtime_lookup_function(instance, "on_headers_complete"))) {
        ctx->on_headers_complete = func;
        ctx->http_settings.on_headers_complete = on_headers_complete;
    }

    if ((func = wasm_runtime_lookup_function(instance, "on_body"))) {
        ctx->on_body = func;
        ctx->http_settings.on_body = on_body;
    }

    if ((func =
             wasm_runtime_lookup_function(instance, "on_message_complete"))) {
        ctx->on_message_complete = func;
        ctx->http_settings.on_message_complete = on_message_complete;
    }

    if ((func = wasm_runtime_lookup_function(instance, "on_error"))) {
        ctx->on_error = func;
    }
}

static inline void
init_pg(Context *ctx, wasm_module_inst_t instance) {
    wasm_function_inst_t func;

    if ((func = wasm_runtime_lookup_function(instance, "get_queries"))) {
        ctx->get_queries = func;
    }

    if ((func = wasm_runtime_lookup_function(instance, "as_raw_datum"))) {
        ctx->as_raw_datum = func;

        wasm_func_type_t func_type =
            wasm_runtime_get_function_type(func, instance->module_type);
        ctx->as_datum = wasm_func_type_get_param_type(func_type, 0).heap_type;
        ctx->raw_datum = wasm_func_type_get_result_type(func_type, 0).heap_type;
    }
}

static inline void
on_readable() {
    pgsocket client;
    char *resp;
    Datum name_datum;
    int ret;
    bool isnull;
    bytea *bin_code;
    LoadArgs load_args = { 0 };
    char error_buf[128];
    wasm_module_t module;
    wasm_module_inst_t instance;
    wasm_exec_env_t exec_env;
    wasm_function_inst_t start_func;

    if (recvmsg(sock, &fd_msg.msg, 0) < 0) {
        ereport(FATAL,
                (errmsg("rustica-%d: failed to recvmsg: %m", worker_id)));
    }
    client = *((int *)CMSG_DATA(fd_msg.cmsg));

    if (rst_database == NULL) {
        ereport(DEBUG1,
                (errmsg("rustica-%d: received job: fd=%d", worker_id, client)));
        resp = "HTTP/1.0 404 Not Found\r\nContent-Length: "
               "37\r\n\r\nrustica.database is never configured\n";
        send(client, resp, strlen(resp), 0);
        goto finally1;
    }

    SetCurrentStatementStartTimestamp();
    StartTransactionCommand();

    SPI_connect();
    PushActiveSnapshot(GetTransactionSnapshot());
    debug_query_string = load_module_sql;
    pgstat_report_activity(STATE_RUNNING, "loading WASM application");

    module = wasm_runtime_find_module_registered("main");
    if (module == NULL) {
        ereport(DEBUG1,
                (errmsg("rustica-%d: load module \"main\"", worker_id)));
        name_datum = CStringGetTextDatum("main");
        ret = SPI_execute_plan(load_module_plan, &name_datum, NULL, true, 1);
        if (ret != SPI_OK_SELECT || SPI_processed == 0) {
            resp = "HTTP/1.0 404 Not Found\r\nContent-Length: "
                   "23\r\n\r\ncan't find main module\n";
            send(client, resp, strlen(resp), 0);
            goto finally2;
        }

        bin_code = DatumGetByteaPP(SPI_getbinval(SPI_tuptable->vals[0],
                                                 SPI_tuptable->tupdesc,
                                                 1,
                                                 &isnull));
        debug_query_string = NULL;

        load_args.name = "";
        load_args.wasm_binary_freeable = true;

        MemoryContext tx_mctx = MemoryContextSwitchTo(TopMemoryContext);
        module = wasm_runtime_load_ex((uint8 *)VARDATA_ANY(bin_code),
                                      VARSIZE_ANY_EXHDR(bin_code),
                                      &load_args,
                                      error_buf,
                                      sizeof(error_buf));
        if (!module) {
            MemoryContextSwitchTo(tx_mctx);
            ereport(WARNING, (errmsg("bad WASM bin_code: %s", error_buf)));
            resp = "HTTP/1.0 500 Internal Server Error\r\nContent-Length: "
                   "13\r\n\r\nBad bin_code\n";
            send(client, resp, strlen(resp), 0);
            goto finally2;
        }
        // due to the lazy loading of `rtt_types` objects which are allocated
        // in the module's instantiate phase and released with popping
        // transactional memory context, potential crashes can occur.
        // To resolve this issue, we preload all rtt_type objects within the
        // TopMemoryContext.
        AOTModule *aot_module = module;
        for (uint32 i = 0; i < aot_module->type_count; i++) {
            if (!wasm_rtt_type_new(aot_module->types[i],
                                   i,
                                   aot_module->rtt_types,
                                   aot_module->type_count,
                                   &aot_module->rtt_type_lock)) {
                wasm_runtime_unload(module);
                MemoryContextSwitchTo(tx_mctx);
                ereport(WARNING, errmsg("create rtt object failed"));
                resp = "HTTP/1.0 500 Internal Server Error\r\nContent-Length: "
                       "28\r\n\r\nFailed to create rtt object\n";
                send(client, resp, strlen(resp), 0);
                goto finally2;
            }
        }
        wasm_runtime_register_module("main",
                                     module,
                                     error_buf,
                                     sizeof(error_buf));
        MemoryContextSwitchTo(tx_mctx);
    }

    pgstat_report_activity(STATE_RUNNING, "running WASM application");

    instance = wasm_runtime_instantiate(module,
                                        64 * 1024,
                                        1024 * 1024,
                                        error_buf,
                                        sizeof(error_buf));
    if (!instance) {
        ereport(WARNING, (errmsg("failed to instantiate: %s", error_buf)));
        resp = "HTTP/1.0 500 Internal Server Error\r\nContent-Length: "
               "21\r\n\r\nInstantiation failed\n";
        send(client, resp, strlen(resp), 0);
        goto finally2;
    }

    exec_env = wasm_runtime_create_exec_env(instance, 64 * 1024);
    if (!exec_env) {
        ereport(WARNING, (errmsg("failed to instantiate WASM application")));
        resp = "HTTP/1.0 500 Internal Server Error\r\nContent-Length: "
               "17\r\n\r\nExecution failed\n";
        send(client, resp, strlen(resp), 0);
        goto finally3;
    }

    Context context = { 0 };
    context.fd = client;
    wasm_runtime_set_user_data(exec_env, &context);
    context.wait_set = CreateWaitEventSet(CurrentMemoryContext, 2);
    AddWaitEventToSet(context.wait_set,
                      WL_LATCH_SET,
                      PGINVALID_SOCKET,
                      MyLatch,
                      NULL);
    AddWaitEventToSet(context.wait_set, WL_SOCKET_CLOSED, client, NULL, NULL);

    init_llhttp(&context, instance);
    context.http_parser.data = exec_env;

    init_pg(&context, instance);

    start_func = wasm_runtime_lookup_function(instance, "_start");
    if (start_func) {
        if (!wasm_runtime_call_wasm(exec_env, start_func, 0, NULL)) {
            ereport(WARNING, errmsg("failed to run _start"));
        }
    }

    start_func = wasm_runtime_lookup_function(instance, "init_modules");
    if (start_func) {
        wasm_val_t rv;
        if (!wasm_runtime_call_wasm_a(exec_env, start_func, 1, &rv, 0, NULL)) {
            ereport(WARNING, errmsg("failed to run init_modules"));
        }
        if (rv.of.i32 == -1) {
            free_app_plans();
        }
    }

    start_func = wasm_runtime_lookup_function(instance, "run");
    if (!start_func) {
        ereport(WARNING, (errmsg("cannot find WASM entrypoint")));
        resp = "HTTP/1.0 500 Internal Server Error\r\nContent-Length: "
               "21\r\n\r\nBad WASM application\n";
        send(client, resp, strlen(resp), 0);
        goto finally4;
    }
    if (!wasm_runtime_call_wasm(exec_env, start_func, 0, NULL)) {
        resp = "HTTP/1.0 500 Internal Server Error\r\nContent-Length: "
               "32\r\n\r\nFailed to run WASM application\n";
        send(client, resp, strlen(resp), 0);
    }

finally4:
    wasm_runtime_destroy_exec_env(exec_env);

finally3:
    wasm_runtime_deinstantiate(instance);

finally2:
    SPI_finish();
    PopActiveSnapshot();
    CommitTransactionCommand();
    debug_query_string = NULL;
    pgstat_report_stat(true);
    pgstat_report_activity(STATE_IDLE, NULL);
    tuptables_size = 0;

finally1:
    StreamClose(client);
    state = WAIT_WRITE;
    ModifyWaitEvent(wait_set, 1, WL_SOCKET_WRITEABLE | WL_SOCKET_CLOSED, NULL);
}

static void
invalidate_cached_module(const char *module_name) {
    ereport(
        DEBUG1,
        (errmsg("rustica-%d: unload module \"%s\"", worker_id, module_name)));
    wasm_module_t module = wasm_runtime_find_module_registered(module_name);
    if (module) {
        // since `wasm_runtime_unload` function does not call `aot_unload` when
        // multi-modules is enabled, we have to call `aot_unload` directly here
        aot_unload(module);
        wasm_runtime_unregister_module(module);
    }
    if (strcmp(module_name, "main") == 0)
        free_app_plans();
}

static int
mock_comm_putmessage(char msgtype, const char *s, size_t len) {
    StringInfoData msg = { .data = s,
                           .len = (int)len,
                           .maxlen = (int)len,
                           .cursor = 0 };
    if (msgtype == 'A') {
        pq_getmsgint(&msg, 4); // pid
        char *channel = pq_getmsgstring(&msg);
        if (strcmp(channel, "rustica_module_cache_invalidation") == 0) {
            char *payload = pq_getmsgstring(&msg);
            invalidate_cached_module(payload);
        }
    }
    return 0;
}

static const PQcommMethods mock_comm_methods = {
    NULL, NULL, NULL, NULL, mock_comm_putmessage, NULL,
};

static void
on_notification_received() {
    ereport(
        DEBUG1,
        (errmsg(
            "rustica-%d: received notification for module cache invalidation",
            worker_id)));

    PQcommMethods *old_methods = PqCommMethods;
    PqCommMethods = &mock_comm_methods;
    whereToSendOutput = DestRemote;
    ProcessNotifyInterrupt(false);
    whereToSendOutput = DestNone;
    PqCommMethods = old_methods;
}

static void
main_loop() {
    WaitEvent events[2];
    int nevents;
    long timeout;

    if (rst_worker_idle_timeout == 0)
        timeout = -1;
    else
        timeout = rst_worker_idle_timeout * 1000;
    for (;;) {
        nevents = WaitEventSetWait(wait_set, timeout, events, 2, 0);
        if (nevents == 0 && state == WAIT_READ) {
            ereport(DEBUG1, (errmsg("rustica-%d: idle timeout", worker_id)));
            return;
        }
        for (int i = 0; i < nevents; i++) {
            if (events[i].events & WL_LATCH_SET) {
                if (shutdown_requested)
                    return;
                ResetLatch(MyLatch);
                if (notify_received) {
                    notify_received = false;
                    on_notification_received();
                }
            }
            if (events[i].events & WL_SOCKET_CLOSED) {
                ereport(DEBUG1,
                        (errmsg("rustica-%d: Unix socket closed", worker_id)));
                return;
            }
            if (state == WAIT_WRITE && events[i].events & WL_SOCKET_WRITEABLE)
                on_writeable();
            if (state == WAIT_READ && events[i].events & WL_SOCKET_READABLE)
                on_readable();
        }
    }
}

static void
teardown() {
    SPI_freeplan(load_module_plan);
    FreeWaitEventSet(wait_set);
    StreamClose(sock);
    sock = PGINVALID_SOCKET;
}

static void
on_sigterm(SIGNAL_ARGS) {
    shutdown_requested = true;
    SetLatch(MyLatch);
}

static void
on_sigusr1(SIGNAL_ARGS) {
    procsignal_sigusr1_handler(postgres_signal_arg);
    if (notifyInterruptPending) {
        notify_received = true;
    }
    SetLatch(MyLatch);
}

PGDLLEXPORT void
rustica_worker(Datum index) {
    pqsignal(SIGTERM, on_sigterm);
    pqsignal(SIGUSR1, on_sigusr1);
    BackgroundWorkerUnblockSignals();

    worker_id = DatumGetInt32(index);
    startup();

    ereport(DEBUG1, (errmsg("rustica-%d: worker started", worker_id)));
    main_loop();
    ereport(DEBUG1, (errmsg("rustica-%d: worker shutting down", worker_id)));

    teardown();
}
