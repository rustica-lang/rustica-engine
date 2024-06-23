#include <sys/socket.h>
#include <sys/un.h>
#include <wchar.h>

#include "postgres.h"
#include <miscadmin.h>
#include <postmaster/bgworker.h>
#include <libpq/libpq.h>
#include "access/xact.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/snapmgr.h"
#include "pgstat.h"
#include "tcop/utility.h"
#include "wasm_runtime_common.h"

#include "rustica_wamr.h"

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
static SPIPlanPtr load_module_plan = NULL;
static const char *load_module_sql =
    "SELECT bin_code FROM rustica.modules WHERE name = $1";

void
spectest_print_char(wasm_exec_env_t exec_env, int c) {
    fwprintf(stderr, L"%lc", c);
}

static NativeSymbol native_symbols[] = {
    { "print_char", spectest_print_char, "(i)" }
};

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
    if (!wasm_runtime_register_natives("spectest", native_symbols, 1))
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
on_readable() {
    pgsocket client;
    char *resp;
    Datum name_datum;
    int ret;
    bool isnull;
    bytea *bin_code;
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

    module = wasm_runtime_load((uint8 *)VARDATA_ANY(bin_code),
                               VARSIZE_ANY_EXHDR(bin_code),
                               error_buf,
                               sizeof(error_buf));
    if (!module) {
        ereport(WARNING, (errmsg("bad WASM bin_code: %s", error_buf)));
        resp = "HTTP/1.0 500 Internal Server Error\r\nContent-Length: "
               "13\r\n\r\nBad bin_code\n";
        send(client, resp, strlen(resp), 0);
        goto finally2;
    }

    pgstat_report_activity(STATE_RUNNING, "running WASM application");

    instance = wasm_runtime_instantiate(module,
                                        1024,
                                        1024,
                                        error_buf,
                                        sizeof(error_buf));
    if (!instance) {
        ereport(WARNING, (errmsg("failed to instantiate: %s", error_buf)));
        resp = "HTTP/1.0 500 Internal Server Error\r\nContent-Length: "
               "21\r\n\r\nInstantiation failed\n";
        send(client, resp, strlen(resp), 0);
        goto finally3;
    }

    exec_env = wasm_runtime_create_exec_env(instance, 1024);
    if (!exec_env) {
        ereport(WARNING, (errmsg("failed to instantiate WASM application")));
        resp = "HTTP/1.0 500 Internal Server Error\r\nContent-Length: "
               "17\r\n\r\nExecution failed\n";
        send(client, resp, strlen(resp), 0);
        goto finally4;
    }

    start_func = wasm_runtime_lookup_function(instance, "_start");
    if (!start_func) {
        ereport(WARNING, (errmsg("cannot find WASM entrypoint")));
        resp = "HTTP/1.0 500 Internal Server Error\r\nContent-Length: "
               "21\r\n\r\nBad WASM application\n";
        send(client, resp, strlen(resp), 0);
        goto finally5;
    }

    if (wasm_runtime_call_wasm(exec_env, start_func, 0, NULL))
        resp = "HTTP/1.0 200 OK\r\nContent-Length: "
               "3\r\n\r\nOK\n";
    else
        resp = "HTTP/1.0 500 Internal Server Error\r\nContent-Length: "
               "32\r\n\r\nFailed to call WASM application\n";
    send(client, resp, strlen(resp), 0);

finally5:
    wasm_runtime_destroy_exec_env(exec_env);

finally4:
    wasm_runtime_deinstantiate(instance);

finally3:
    wasm_runtime_unload(module);

finally2:
    SPI_finish();
    PopActiveSnapshot();
    CommitTransactionCommand();
    debug_query_string = NULL;
    pgstat_report_stat(true);
    pgstat_report_activity(STATE_IDLE, NULL);

finally1:
    StreamClose(client);
    state = WAIT_WRITE;
    ModifyWaitEvent(wait_set, 1, WL_SOCKET_WRITEABLE | WL_SOCKET_CLOSED, NULL);
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

PGDLLEXPORT void
rustica_worker(Datum index) {
    pqsignal(SIGTERM, on_sigterm);
    BackgroundWorkerUnblockSignals();

    worker_id = DatumGetInt32(index);
    startup();

    ereport(DEBUG1, (errmsg("rustica-%d: worker started", worker_id)));
    main_loop();
    ereport(DEBUG1, (errmsg("rustica-%d: worker shutting down", worker_id)));

    teardown();
}
