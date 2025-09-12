/*
 * SPDX-FileCopyrightText: 2024 燕几（北京）科技有限公司
 * SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0
 */

#include <sys/socket.h>

#include "postgres.h"
#include "utils/varlena.h"
#include "libpq/libpq.h"
#include "miscadmin.h"
#include "common/ip.h"
#include "postmaster/bgworker.h"
#include "postmaster/postmaster.h"

#include "rustica/event_set.h"
#include "rustica/gucs.h"
#include "rustica/utils.h"

typedef struct Socket Socket;

#define TYPE_UNSET 0
#define TYPE_IPC 1
#define TYPE_FRONTEND 2
#define TYPE_BACKEND 3
#define MAXLISTEN 64
#define JOB_QLEN 1024
static WaitEventSetEx *rm_wait_set = NULL;
static Socket *sockets;
static int total_sockets = 0;
static bool shutdown_requested = false;
static bool worker_died = false;
static int *idle_workers;
static int idle_qhead = 0, idle_qtail = 0, idle_qsize = 0;
static int num_workers;
static BackgroundWorkerHandle **worker_handles;
static FDMessage fd_msg;
static pgsocket job_queue[JOB_QLEN];
static int job_qhead = 0, job_qtail = 0, job_qsize = 0;
static bool frontend_paused = false;
static int worker_id_seq = 0;

typedef struct Socket {
    char type;
    pgsocket fd;
    int pos;

    uint8_t read_offset;
    uint32_t worker_id;
} Socket;

static int
listen_frontend(pgsocket *listen_sockets) {
    int success, status, nsockets;
    char *addr_string, *addr;
    List *list;
    ListCell *cell;

    for (int i = 0; i < MAXLISTEN; i++)
        listen_sockets[i] = PGINVALID_SOCKET;

    addr_string = pstrdup(rst_listen_addresses);
    if (!SplitGUCList(addr_string, ',', &list)) {
        ereport(FATAL,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("invalid list syntax in parameter \"%s\"",
                        "listen_addresses")));
    }
    success = 0;
    foreach (cell, list) {
        addr = (char *)lfirst(cell);

        if (strcmp(addr, "*") == 0)
            status = StreamServerPort(AF_UNSPEC,
                                      NULL,
                                      (unsigned short)rst_port,
                                      NULL,
                                      listen_sockets,
                                      MAXLISTEN);
        else
            status = StreamServerPort(AF_UNSPEC,
                                      addr,
                                      (unsigned short)rst_port,
                                      NULL,
                                      listen_sockets,
                                      MAXLISTEN);

        if (status == STATUS_OK) {
            success++;
        }
        else
            ereport(
                WARNING,
                (errmsg("could not create listen socket for \"%s\"", addr)));
    }
    if (!success && list != NIL)
        ereport(FATAL, (errmsg("could not create any TCP/IP sockets")));
    list_free(list);
    pfree(addr_string);
    nsockets = 0;
    while (nsockets < MAXLISTEN && listen_sockets[nsockets] != PGINVALID_SOCKET)
        ++nsockets;
    if (nsockets == 0)
        ereport(FATAL, (errmsg("no socket created for listening")));
    return nsockets;
}

static pgsocket
listen_backend() {
    pgsocket ipc_sock;
    struct sockaddr_un addr;
    int err;

    ipc_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipc_sock == PGINVALID_SOCKET)
        ereport(FATAL, (errmsg("could not create Unix-domain socket")));
    rst_make_ipc_addr(&addr);
    err = bind(ipc_sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_un));
    if (err < 0)
        ereport(FATAL,
                (errmsg("could not bind %s address \"%s\": %m",
                        "Unix",
                        &addr.sun_path[1])));
    err = listen(ipc_sock, max_worker_processes * 2);
    if (err < 0)
        ereport(FATAL,
                (errmsg("could not listen on %s address \"%s\": %m",
                        "Unix",
                        &addr.sun_path[1])));

    return ipc_sock;
}

static void
on_sigterm(SIGNAL_ARGS) {
    shutdown_requested = true;
    SetLatch(MyLatch);
}

static void
on_sigusr1(SIGNAL_ARGS) {
    worker_died = true;
    SetLatch(MyLatch);
}

static void
startup() {
    pgsocket listen_sockets[MAXLISTEN], ipc_sock;
    int num_listen_sockets;
    Socket *socket;

    pqsignal(SIGTERM, on_sigterm);
    pqsignal(SIGUSR1, on_sigusr1);
    BackgroundWorkerUnblockSignals();

    memset(&fd_msg, 0, sizeof(FDMessage));
    fd_msg.io.iov_base = &fd_msg.byte;
    fd_msg.io.iov_len = 1;
    fd_msg.msg.msg_iov = &fd_msg.io;
    fd_msg.msg.msg_iovlen = 1;
    fd_msg.msg.msg_control = fd_msg.buf;
    fd_msg.msg.msg_controllen = sizeof(fd_msg.buf);
    fd_msg.cmsg = CMSG_FIRSTHDR(&fd_msg.msg);
    fd_msg.cmsg->cmsg_level = SOL_SOCKET;
    fd_msg.cmsg->cmsg_type = SCM_RIGHTS;
    fd_msg.cmsg->cmsg_len = CMSG_LEN(sizeof(int));

    num_listen_sockets = listen_frontend(listen_sockets);
    ipc_sock = listen_backend();
    total_sockets = 1 + num_listen_sockets + max_worker_processes;

    sockets = (Socket *)MemoryContextAllocZero(CurrentMemoryContext,
                                               sizeof(Socket) * total_sockets);
    rm_wait_set = CreateWaitEventSetEx(CurrentMemoryContext, total_sockets);
    idle_workers = (int *)MemoryContextAllocZero(CurrentMemoryContext,
                                                 sizeof(int) * total_sockets);
    worker_handles = (BackgroundWorkerHandle **)MemoryContextAllocZero(
        CurrentMemoryContext,
        sizeof(BackgroundWorkerHandle *) * max_worker_processes);

    socket = &sockets[NextWaitEventPos(rm_wait_set)];
    socket->type = TYPE_UNSET;
    socket->fd = -1;
    socket->pos = AddWaitEventToSetEx(rm_wait_set,
                                      WL_LATCH_SET,
                                      PGINVALID_SOCKET,
                                      MyLatch,
                                      socket);
    Assert(socket->pos != -1);

    socket = &sockets[NextWaitEventPos(rm_wait_set)];
    socket->type = TYPE_IPC;
    socket->fd = ipc_sock;
    socket->pos = AddWaitEventToSetEx(rm_wait_set,
                                      WL_SOCKET_ACCEPT,
                                      socket->fd,
                                      NULL,
                                      socket);
    Assert(socket->pos != -1);

    for (int i = 0; i < num_listen_sockets; i++) {
        if (listen_sockets[i] == PGINVALID_SOCKET)
            ereport(FATAL, (errmsg("no socket created for listening")));
        socket = &sockets[NextWaitEventPos(rm_wait_set)];
        socket->type = TYPE_FRONTEND;
        socket->fd = listen_sockets[i];
        socket->pos = AddWaitEventToSetEx(rm_wait_set,
                                          WL_SOCKET_ACCEPT,
                                          socket->fd,
                                          NULL,
                                          socket);
        Assert(socket->pos != -1);
    }
}

static inline void
on_backend_connect(Socket *socket, uint32 events) {
    pgsocket sock;
    SockAddr addr;

    if (!(events & WL_SOCKET_ACCEPT))
        return;

    ereport(DEBUG1,
            (errmsg("accept backend connection from: fd=%d", socket->fd)));
    addr.salen = sizeof(addr.addr);
    sock = accept(socket->fd, (struct sockaddr *)&addr.addr, &addr.salen);
    if (sock == PGINVALID_SOCKET) {
        ereport(LOG,
                (errcode_for_socket_access(),
                 errmsg("could not accept new connection: %m")));
        pg_usleep(100000L); // wait 0.1 sec
        return;
    }

    socket = &sockets[NextWaitEventPos(rm_wait_set)];
    socket->type = TYPE_BACKEND;
    socket->fd = sock;
    socket->pos = AddWaitEventToSetEx(rm_wait_set,
                                      WL_SOCKET_READABLE | WL_SOCKET_CLOSED,
                                      sock,
                                      NULL,
                                      socket);
    if (socket->pos != -1) {
        ereport(DEBUG1, (errmsg("backend connection received: fd=%d", sock)));
    }
    else {
        ereport(WARNING, (errmsg("too many backend connections: fd=%d", sock)));
        StreamClose(sock);
    }
}

static inline void
close_socket(Socket *socket) {
    DeleteWaitEventEx(rm_wait_set, socket->pos);
    StreamClose(socket->fd);
    memset(socket, 0, sizeof(Socket));
}

static inline void
on_frontend(Socket *socket, uint32 events) {
    pgsocket sock;
    SockAddr addr;
    Socket *backend;

    if (!(events & WL_SOCKET_ACCEPT))
        return;

    addr.salen = sizeof(addr.addr);
    sock = accept(socket->fd, (struct sockaddr *)&addr.addr, &addr.salen);
    if (sock == PGINVALID_SOCKET) {
        ereport(LOG,
                (errcode_for_socket_access(),
                 errmsg("could not accept new connection: %m")));
        pg_usleep(100000L); // wait 0.1 sec
        return;
    }
    ereport(DEBUG1,
            (errmsg("accepted frontend connection fd=%d from: fd=%d",
                    sock,
                    socket->fd)));
    if (Log_connections) {
        int ret;
        char remote_host[NI_MAXHOST];
        char remote_port[NI_MAXSERV];
        remote_host[0] = '\0';
        remote_port[0] = '\0';
        ret = pg_getnameinfo_all(&addr.addr,
                                 addr.salen,
                                 remote_host,
                                 sizeof(remote_host),
                                 remote_port,
                                 sizeof(remote_port),
                                 (log_hostname ? 0 : NI_NUMERICHOST)
                                     | NI_NUMERICSERV);
        if (ret != 0)
            ereport(WARNING,
                    (errmsg_internal("pg_getnameinfo_all() failed: %s",
                                     gai_strerror(ret))));
        ereport(LOG,
                (errmsg("connection received: host=%s port=%s",
                        remote_host,
                        remote_port)));
    }
    while (idle_qsize > 0) {
        backend = &sockets[idle_workers[idle_qhead]];
        idle_qhead = (idle_qhead + 1) % total_sockets;
        idle_qsize -= 1;
        if (backend->type == TYPE_BACKEND) {
            Assert(backend->type == TYPE_BACKEND);
            *((int *)CMSG_DATA(fd_msg.cmsg)) = sock;
            if (sendmsg(backend->fd, &fd_msg.msg, 0) < 0) {
                ereport(DEBUG1,
                        (errmsg("socket (fd=%d) is broken: %m", backend->fd)));
                close_socket(backend);
            }
            else {
                StreamClose(sock);
                ereport(DEBUG1,
                        (errmsg("dispatched job fd=%d to rustica-%d",
                                sock,
                                backend->worker_id)));
                ModifyWaitEventEx(rm_wait_set,
                                  backend->pos,
                                  WL_SOCKET_READABLE | WL_SOCKET_CLOSED,
                                  NULL);
                return;
            }
        }
    }
    if (idle_qsize == 0 && num_workers < max_worker_processes - 2) {
        BackgroundWorker worker;
        BackgroundWorkerHandle **handle;

        snprintf(worker.bgw_name, BGW_MAXLEN, "rustica-%d", worker_id_seq);
        snprintf(worker.bgw_type, BGW_MAXLEN, "rustica worker");
        worker.bgw_flags =
            BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
        worker.bgw_start_time = BgWorkerStart_ConsistentState;
        worker.bgw_restart_time = BGW_NEVER_RESTART;
        snprintf(worker.bgw_library_name, BGW_MAXLEN, "rustica-engine");
        snprintf(worker.bgw_function_name, BGW_MAXLEN, "rustica_worker");
        worker.bgw_notify_pid = MyProcPid;
        worker.bgw_main_arg = Int32GetDatum(worker_id_seq++);

        handle = NULL;
        for (int i = 0; i < max_worker_processes; i++) {
            if (worker_handles[i] == NULL) {
                handle = &worker_handles[i];
                break;
            }
        }
        Assert(handle != NULL);
        if (RegisterDynamicBackgroundWorker(&worker, handle)) {
            num_workers++;
        }
    }
    if (job_qsize < JOB_QLEN) {
        job_qsize++;
        job_queue[job_qtail] = sock;
        job_qtail = (job_qtail + 1) % JOB_QLEN;
        if (job_qsize == JOB_QLEN) {
            ereport(DEBUG1,
                    (errmsg("job queue is full, pause accepting frontend "
                            "connections")));
            frontend_paused = true;
            for (int i = 0; i < total_sockets; i++) {
                if (sockets[i].type == TYPE_FRONTEND) {
                    ModifyWaitEventEx(rm_wait_set,
                                      sockets[i].pos,
                                      WL_SOCKET_CLOSED,
                                      NULL);
                }
            }
        }
    }
    else {
        ereport(DEBUG1, (errmsg("job queue is full, closing fd=%d", sock)));
        StreamClose(sock);
    }
}

static inline void
on_backend(Socket *socket, uint32 events) {
    pgsocket job;

    if (events & WL_SOCKET_CLOSED) {
        ereport(DEBUG1, (errmsg("socket is closed: fd=%d", socket->fd)));
        close_socket(socket);
    }
    else if (events & WL_SOCKET_READABLE) {
        char buf[12];
        int i;
        ssize_t received;

        if (socket->read_offset >= 12) {
            ModifyWaitEventEx(rm_wait_set, socket->pos, WL_SOCKET_CLOSED, NULL);
            return;
        }
        received = recv(socket->fd, &buf, 12 - socket->read_offset, 0);
        if (received < 0) {
            ereport(DEBUG1, (errmsg("failed in recv fd=%d: %m", socket->fd)));
            close_socket(socket);
        }
        for (i = 0; i < Min(8 - socket->read_offset, received); i++) {
            if (BACKEND_HELLO[socket->read_offset + i] != buf[i]) {
                ereport(LOG,
                        (errmsg("Bad hello from backend: fd=%d", socket->fd)));
                close_socket(socket);
                return;
            }
        }
        if (received - i > 0) {
            memcpy(((char *)&socket->worker_id)
                       + Max(0, socket->read_offset - 8),
                   buf + i,
                   received - i);
            if (socket->read_offset + received == 12) {
                socket->read_offset = 0;

                if (job_qsize > 0) {
                    job = job_queue[job_qhead];
                    *((int *)CMSG_DATA(fd_msg.cmsg)) = job_queue[job_qhead];
                    if (sendmsg(socket->fd, &fd_msg.msg, 0) < 0) {
                        ereport(DEBUG1,
                                (errmsg("socket (fd=%d) is broken: %m",
                                        socket->fd)));
                        close_socket(socket);
                    }
                    else {
                        job_qsize--;
                        job_qhead = (job_qhead + 1) % JOB_QLEN;
                        ereport(DEBUG1,
                                (errmsg("dispatched job fd=%d to rustica-%d",
                                        job,
                                        socket->worker_id)));
                        if (frontend_paused) {
                            ereport(DEBUG1,
                                    (errmsg("resume accepting frontend "
                                            "connections")));
                            frontend_paused = false;
                            for (i = 0; i < total_sockets; i++) {
                                if (sockets[i].type == TYPE_FRONTEND) {
                                    ModifyWaitEventEx(rm_wait_set,
                                                      sockets[i].pos,
                                                      WL_SOCKET_ACCEPT,
                                                      NULL);
                                }
                            }
                        }
                    }
                }
                else {
                    ModifyWaitEventEx(rm_wait_set,
                                      socket->pos,
                                      WL_SOCKET_CLOSED,
                                      NULL);
                    Assert(idle_qsize < total_sockets);
                    idle_qsize++;
                    idle_workers[idle_qtail] = socket->pos;
                    idle_qtail = (idle_qtail + 1) % total_sockets;
                    ereport(DEBUG1,
                            (errmsg("rustica-%d is idle", socket->worker_id)));
                }
                return;
            }
        }
        socket->read_offset = (uint8_t)(socket->read_offset + received);
    }
}

static void
on_worker_died() {
    int workers = 0;
    for (int i = 0; i < max_worker_processes; i++) {
        BgwHandleStatus status;
        pid_t pid;
        if (worker_handles[i] != NULL) {
            status = GetBackgroundWorkerPid(worker_handles[i], &pid);
            if (status == BGWH_STOPPED) {
                pfree(worker_handles[i]);
                worker_handles[i] = NULL;
            }
            else {
                workers++;
            }
        }
    }
    if (workers < num_workers) {
        ereport(DEBUG1,
                (errmsg("%d rustica workers have exited, %d remaining",
                        num_workers - workers,
                        workers)));
        num_workers = workers;
    }
    else {
        Assert(workers == num_workers);
    }
}

static void
main_loop() {
    WaitEvent events[MAXLISTEN];
    int nevents;
    Socket *socket;

    for (;;) {
        nevents =
            WaitEventSetWaitEx(rm_wait_set, -1, events, lengthof(events), 0);
        for (int i = 0; i < nevents; i++) {
            socket = (Socket *)events[i].user_data;
            if (events[i].events & WL_LATCH_SET) {
                if (shutdown_requested) {
                    return;
                }
                ResetLatch(MyLatch);
                if (worker_died) {
                    worker_died = false;
                    on_worker_died();
                }
            }
            if (socket->type == TYPE_IPC)
                on_backend_connect(socket, events[i].events);
            if (socket->type == TYPE_FRONTEND)
                on_frontend(socket, events[i].events);
            if (socket->type == TYPE_BACKEND)
                on_backend(socket, events[i].events);
        }
    }
}

static void
teardown() {
    ereport(LOG, (errmsg("rustica master shutting down")));
    pfree(worker_handles);
    pfree(idle_workers);
    FreeWaitEventSetEx(rm_wait_set);
    rm_wait_set = NULL;

    for (int i = 0; i < total_sockets; i++) {
        if (sockets[i].type != TYPE_UNSET) {
            sockets[i].type = TYPE_UNSET;
            StreamClose(sockets[i].fd);
        }
    }

    pfree(sockets);
}

PGDLLEXPORT void
rustica_master() {
    startup();
    main_loop();
    teardown();
}
