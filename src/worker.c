#include <sys/socket.h>
#include <sys/un.h>

#include "postgres.h"
#include <miscadmin.h>
#include <postmaster/bgworker.h>
#include <libpq/libpq.h>

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

static void
startup() {
    struct sockaddr_un addr;

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
on_readable() {}

static void
main_loop() {
    WaitEvent events[2];
    int nevents;

    for (;;) {
        nevents = WaitEventSetWait(wait_set, -1, events, 2, 0);
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
