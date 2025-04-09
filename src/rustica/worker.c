/*
 * Copyright (c) 2024-present 燕几（北京）科技有限公司
 *
 * Rustica Engine is licensed under Mulan PSL v2. You can use this
 * software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *              https://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
 * OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the Mulan PSL v2 for more details.
 */

#include <sys/un.h>

#include "postgres.h"
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "libpq/libpq.h"
#include "libpq/pqformat.h"
#include "access/xact.h"
#include "commands/async.h"
#include "tcop/utility.h"
#include "utils/snapmgr.h"
#ifdef RUSTICA_SQL_BACKDOOR
#include "utils/builtins.h"
#include "utils/jsonb.h"
#endif
#include "pgstat.h"

#include "llhttp.h"

#include "rustica/datatypes.h"
#include "rustica/gucs.h"
#include "rustica/module.h"
#include "rustica/query.h"
#include "rustica/utils.h"
#include "rustica/wamr.h"

#define WAIT_WRITE 0
#define WAIT_READ 0
static int worker_id;
static pgsocket sock;
static char hello[12];
static WaitEventSet *wait_set = NULL;
static bool shutdown_requested = false;
static char state = WAIT_WRITE;
static int sent = 0;
static FDMessage fd_msg;

static int32_t
env_recv(wasm_exec_env_t exec_env,
         wasm_obj_t refobj,
         int32_t start,
         int32_t len) {
    WaitEvent events[1];
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    Datum bytes = wasm_externref_obj_get_datum(refobj, BYTEAOID);
    char *view = VARDATA_ANY(DatumGetPointer(bytes));
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
         wasm_obj_t refobj,
         int32_t start,
         int32_t len) {
    WaitEvent events[1];
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    Datum bytes = wasm_externref_obj_get_datum(refobj, BYTEAOID);
    char *view = VARDATA_ANY(DatumGetPointer(bytes));
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
    const char *reason = llhttp_get_error_reason(&ctx->http_parser);
    wasm_externref_obj_t bytes =
        cstring_into_varatt_obj(exec_env, reason, strlen(reason), BYTEAOID);
    wasm_val_t args[1] = { { .kind = WASM_EXTERNREF,
                             .of.foreign = (uintptr_t)bytes } };
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
                   wasm_obj_t buf,
                   int32_t start,
                   int32_t len) {
    Datum bytes = wasm_externref_obj_get_datum(buf, BYTEAOID);
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    char *view = VARDATA_ANY(DatumGetPointer(bytes));
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
env_llhttp_finish(wasm_exec_env_t exec_env, wasm_obj_t buf) {
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
env_llhttp_get_error_pos(wasm_exec_env_t exec_env, wasm_obj_t buf) {
    Context *ctx = wasm_runtime_get_user_data(exec_env);
    Datum bytes = wasm_externref_obj_get_datum(buf, BYTEAOID);
    char *view = VARDATA_ANY(DatumGetPointer(bytes));
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

#ifdef RUSTICA_SQL_BACKDOOR
wasm_externref_obj_t
env_sql_backdoor(wasm_exec_env_t exec_env,
                 wasm_obj_t refobj,
                 int32_t start,
                 int32_t len) {
    char *resp;
    int res;
    PG_TRY();
    {
        Datum bytes = wasm_externref_obj_get_datum(refobj, BYTEAOID);
        char *view = VARDATA_ANY(DatumGetPointer(bytes)) + start;
        if (view[len - 1] != '\0') {
            char *sql = (char *)palloc(len + 1);
            memcpy(sql, view, len);
            sql[len] = '\0';
            view = sql;
        }
        ereport(DEBUG1, errmsg("backdoor execute SQL: %s", view));

        res = SPI_execute(view, false, 0);
        if (res < 0)
            ereport(ERROR, errmsg("SPI_execute failed, errcode: %d", res));

        bool isnull = true;
        if (SPI_tuptable == NULL) {
            resp = "null";
        }
        else {
            TupleDesc tupdesc = SPI_tuptable->tupdesc;
            Datum datum;
            if (SPI_tuptable->numvals == 1 && tupdesc->natts == 1
                && (tupdesc->attrs[0].atttypid == JSONBOID
                    || tupdesc->attrs[0].atttypid == JSONOID)) {
                datum = SPI_getbinval(SPI_tuptable->vals[0],
                                      SPI_tuptable->tupdesc,
                                      1,
                                      &isnull);
            }
            if (isnull) {
                resp = "null";
            }
            else if (tupdesc->attrs[0].atttypid == JSONBOID) {
                Jsonb *json = DatumGetJsonbP(datum);
                resp = JsonbToCString(NULL, &json->root, VARSIZE(json));
            }
            else if (tupdesc->attrs[0].atttypid == JSONOID) {
                resp = TextDatumGetCString(datum);
            }
            else {
                ereport(
                    ERROR,
                    errmsg("unknown atttype: %d", tupdesc->attrs[0].atttypid));
                resp = "null";
            }
        }
    }
    PG_CATCH();
    {
        ErrorData *edata = CopyErrorData();
        resp = psprintf("{\"errcode\": %d, \"message\": \"%s\"}",
                        edata->sqlerrcode,
                        edata->message);
        FreeErrorData(edata);
    }
    PG_END_TRY();

    return cstring_into_varatt_obj(exec_env, resp, strlen(resp), BYTEAOID);
}
#endif

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
    { "ereport", env_ereport, "(ir)i" },
#ifdef RUSTICA_SQL_BACKDOOR
    { "tid_to_oid", env_tid_to_oid, "(r)i" },
    { "sql_backdoor", env_sql_backdoor, "(rii)r" },
#endif
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
    Datum bytes = wasm_externref_obj_get_datum(ctx->current_buf, BYTEAOID);
    start.i32 = (int32_t)(at - VARDATA_ANY(DatumGetPointer(bytes)));
    len.i32 = (int32_t)length;
    wasm_struct_obj_set_field(view, 0, &buf_ref);
    wasm_struct_obj_set_field(view, 1, &start);
    wasm_struct_obj_set_field(view, 2, &len);
    wasm_val_t results[1];
    wasm_val_t args[1] = {
        { .kind = WASM_EXTERNREF, .of.foreign = (uintptr_t)view },
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
                            LoadArgs *load_args,
                            uint8 **p_buffer,
                            uint32 *p_size) {
    ereport(LOG,
            (errmsg("wasm_module_reader_callback: load WASM dependency \"%s\"",
                    load_args->name)));
    pgstat_report_activity(STATE_RUNNING, "loading WASM dependency");

    bool rv;
    PG_TRY();
    {
        load_args->name =
            (char *)rst_prepare_module(load_args->name, p_buffer, p_size);
        load_args->wasm_binary_freeable = true;
        rv = true;
    }
    PG_CATCH();
    {
        EmitErrorReport();
        rv = false;
    }
    PG_END_TRY();
    return rv;
}

static bool
wasm_module_completer_callback(wasm_module_t module) {
    PreparedModule *pmod =
        (PreparedModule *)wasm_runtime_get_module_name(module);
    pmod->module = (AOTModule *)module;
    SPI_freetuptable(pmod->loading_tuptable);
    pmod->loading_tuptable = NULL;
    return true;
}

static void
wasm_module_destroyer_callback(uint8 *buffer, uint32 size) {}

static void
startup() {
    struct sockaddr_un addr;

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
    rst_make_ipc_addr(&addr);
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

        rst_module_worker_startup();

        SPI_finish();
        CommitTransactionCommand();
    }
    wasm_runtime_unregister_natives("env", rst_noop_native_env);
    REGISTER_WASM_NATIVES("env", native_env);
    wasm_runtime_set_module_reader(wasm_module_reader_callback,
                                   wasm_module_completer_callback,
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

static void
on_readable() {
    // Take a job from the FD channel
    if (recvmsg(sock, &fd_msg.msg, 0) < 0) {
        ereport(FATAL, errmsg("rustica-%d: failed to recvmsg: %m", worker_id));
    }
    pgsocket client = *((int *)CMSG_DATA(fd_msg.cmsg));
    ereport(DEBUG1,
            errmsg("rustica-%d: received job: fd=%d", worker_id, client));

    // Prepare to handle the connection
    bool spi_connected = false;
    wasm_exec_env_t exec_env = NULL;
    bool success = false;

    PG_TRY();
    {
        if (rst_database == NULL)
            ereport(ERROR,
                    errcode(ERRCODE_NO_DATA_FOUND),
                    errmsg("rustica.database is never configured"));

        // Connect to SPI
        SetCurrentStatementStartTimestamp();
        StartTransactionCommand();
        SPI_connect();
        PushActiveSnapshot(GetTransactionSnapshot());
        spi_connected = true;

        // Load module if it's not loaded already
        const char *name = "main";
        PreparedModule *pmod = rst_lookup_module(name);
        if (!pmod) {
            pgstat_report_activity(STATE_RUNNING, "loading WASM application");
            ereport(DEBUG1,
                    errmsg("rustica-%d: load module \"%s\"", worker_id, name));
            pmod = rst_prepare_module(name, NULL, NULL);
        }

        // Instantiate the WASM module
        pgstat_report_activity(STATE_RUNNING, "running WASM application");
        exec_env = rst_module_instantiate(pmod, 256 * 1024, 1024 * 1024);

        // Prepare context for execution
        Context context = { .fd = client, .module = pmod };
        wasm_runtime_set_user_data(exec_env, &context);
        context.wait_set = CreateWaitEventSet(CurrentMemoryContext, 2);
        AddWaitEventToSet(context.wait_set,
                          WL_LATCH_SET,
                          PGINVALID_SOCKET,
                          MyLatch,
                          NULL);
        AddWaitEventToSet(context.wait_set,
                          WL_SOCKET_CLOSED,
                          client,
                          NULL,
                          NULL);

        // Initialize context
        rst_init_instance_context(exec_env);
        rst_init_context_for_jsonb(exec_env);
        wasm_module_inst_t instance = wasm_exec_env_get_module_inst(exec_env);
        init_llhttp(&context, instance);
        context.http_parser.data = exec_env;

        // Run the WASM module instance
        wasm_function_inst_t start_func =
            wasm_runtime_lookup_function(instance, "_start");
        if (!start_func)
            ereport(ERROR, errmsg("cannot find WASM entrypoint"));
        success = wasm_runtime_call_wasm(exec_env, start_func, 0, NULL);
    }
    PG_FINALLY();
    {
        if (exec_env) {
            wasm_module_inst_t instance =
                wasm_exec_env_get_module_inst(exec_env);
            wasm_runtime_deinstantiate(instance);
            rst_free_instance_context(exec_env);
            wasm_runtime_destroy_exec_env(exec_env);
        }

        if (spi_connected) {
            SPI_finish();
            PopActiveSnapshot();
            if (success && !_do_rethrow)
                CommitTransactionCommand();
            else
                AbortCurrentTransaction();
            pgstat_report_stat(true);
            pgstat_report_activity(STATE_IDLE, NULL);
        }

        StreamClose(client);
        state = WAIT_WRITE;
        ModifyWaitEvent(wait_set,
                        1,
                        WL_SOCKET_WRITEABLE | WL_SOCKET_CLOSED,
                        NULL);
    }
    PG_END_TRY();
}

static void
invalidate_cached_module(const char *module_name) {
    ereport(
        DEBUG1,
        (errmsg("rustica-%d: unload module \"%s\"", worker_id, module_name)));
    PreparedModule *module = rst_lookup_module(module_name);
    if (module)
        rst_free_module(module);
}

static int
mock_comm_putmessage(char msgtype, const char *s, size_t len) {
    StringInfoData msg = { .data = (char *)s,
                           .len = (int)len,
                           .maxlen = (int)len,
                           .cursor = 0 };
    if (msgtype == 'A') {
        pq_getmsgint(&msg, 4); // pid
        const char *channel = pq_getmsgstring(&msg);
        if (strcmp(channel, "rustica_module_cache_invalidation") == 0) {
            const char *payload = pq_getmsgstring(&msg);
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
    const PQcommMethods *old_methods = PqCommMethods;
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

        if (notifyInterruptPending) {
            on_notification_received();
        }
    }
}

static void
teardown() {
    rst_module_worker_teardown();
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
