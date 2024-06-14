#include <sys/socket.h>

#include "postgres.h"
#include "utils/varlena.h"
#include "libpq/libpq.h"
#include "miscadmin.h"
#include "common/ip.h"
#include "postmaster/bgworker.h"
#include "postmaster/postmaster.h"

#include "rustica_wamr.h"

#define MAXLISTEN 64
static pgsocket listen_sockets[MAXLISTEN];
static WaitEventSet *rm_wait_set = NULL;

static void
startup() {
    int status;

    BackgroundWorkerUnblockSignals();

    for (int i = 0; i < MAXLISTEN; i++)
        listen_sockets[i] = PGINVALID_SOCKET;

    char *addr_string = pstrdup(rst_listen_addresses);
    List *list;
    ListCell *cell;
    int success = 0;
    if (!SplitGUCList(addr_string, ',', &list)) {
        ereport(FATAL,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("invalid list syntax in parameter \"%s\"",
                        "listen_addresses")));
    }
    foreach (cell, list) {
        char *addr = (char *)lfirst(cell);

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
    int nsockets = 0;
    while (nsockets < MAXLISTEN && listen_sockets[nsockets] != PGINVALID_SOCKET)
        ++nsockets;
    if (nsockets == 0)
        ereport(FATAL, (errmsg("no socket created for listening")));
    rm_wait_set = CreateWaitEventSet(CurrentMemoryContext, 1 + nsockets);
    AddWaitEventToSet(rm_wait_set,
                      WL_LATCH_SET,
                      PGINVALID_SOCKET,
                      MyLatch,
                      NULL);
    for (int i = 0; i < nsockets; i++) {
        if (listen_sockets[i] == PGINVALID_SOCKET)
            ereport(FATAL, (errmsg("no socket created for listening")));
        AddWaitEventToSet(rm_wait_set,
                          WL_SOCKET_ACCEPT,
                          listen_sockets[i],
                          NULL,
                          NULL);
    }
}

static void
main_loop() {
    WaitEvent events[MAXLISTEN];
    int nevents;

    for (;;) {
        nevents =
            WaitEventSetWait(rm_wait_set, -1, events, lengthof(events), 0);
        for (int i = 0; i < nevents; i++) {
            if (events[i].events & WL_LATCH_SET)
                ResetLatch(MyLatch);

            if (events[i].events & WL_SOCKET_ACCEPT) {
                pgsocket sock;
                SockAddr remote_addr;
                remote_addr.salen = sizeof(remote_addr.addr);
                if ((sock = accept(events[i].fd,
                                   (struct sockaddr *)&remote_addr.addr,
                                   &remote_addr.salen))
                    == PGINVALID_SOCKET) {
                    ereport(LOG,
                            (errcode_for_socket_access(),
                             errmsg("could not accept new connection: %m")));
                    pg_usleep(100000L); /* wait 0.1 sec */
                }
                else {
                    if (Log_connections) {
                        int ret;
                        char remote_host[NI_MAXHOST];
                        char remote_port[NI_MAXSERV];
                        remote_host[0] = '\0';
                        remote_port[0] = '\0';
                        if ((ret = pg_getnameinfo_all(
                                 &remote_addr.addr,
                                 remote_addr.salen,
                                 remote_host,
                                 sizeof(remote_host),
                                 remote_port,
                                 sizeof(remote_port),
                                 (log_hostname ? 0 : NI_NUMERICHOST)
                                     | NI_NUMERICSERV))
                            != 0)
                            ereport(WARNING,
                                    (errmsg_internal(
                                        "pg_getnameinfo_all() failed: %s",
                                        gai_strerror(ret))));
                        ereport(LOG,
                                (errmsg("connection received: host=%s port=%s",
                                        remote_host,
                                        remote_port)));
                    }
                    StreamClose(sock);
                }
            }
        }
    }
}

static void
teardown() {
    FreeWaitEventSet(rm_wait_set);
    rm_wait_set = NULL;
    for (int i = 0; i < MAXLISTEN; i++) {
        if (listen_sockets[i] != PGINVALID_SOCKET) {
            StreamClose(listen_sockets[i]);
            listen_sockets[i] = PGINVALID_SOCKET;
        }
    }
}

PGDLLEXPORT void
rustica_master() {
    startup();
    main_loop();
    teardown();
}
