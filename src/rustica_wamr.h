#ifndef RUSTICA_WAMR_H
#define RUSTICA_WAMR_H

#include "postgres.h"
#include <storage/latch.h>

#define BACKEND_HELLO "RUSTICA!"

extern char *rst_listen_addresses;
extern int rst_port;

void
rst_init_gucs();

typedef struct WaitEventSetEx WaitEventSetEx;

WaitEventSetEx *
CreateWaitEventSetEx(MemoryContext context, int nevents);

int
AddWaitEventToSetEx(WaitEventSetEx *set,
                    uint32 events,
                    pgsocket fd,
                    Latch *latch,
                    void *user_data);

int
NextWaitEventPos(WaitEventSetEx *set);

void
ModifyWaitEventEx(WaitEventSetEx *set, int pos, uint32 events, Latch *latch);

void
DeleteWaitEventEx(WaitEventSetEx *set, int pos);

int
WaitEventSetWaitEx(WaitEventSetEx *set,
                   long timeout,
                   WaitEvent *occurred_events,
                   int nevents,
                   uint32 wait_event_info);

void
FreeWaitEventSetEx(WaitEventSetEx *set);

void
make_ipc_addr(struct sockaddr_un *addr);

#endif /* RUSTICA_WAMR_H */
