#include "postgres.h"
#include <storage/latch.h>

#ifdef HAVE_SYS_EPOLL_H
#include <sys/epoll.h>
#endif

#include "rustica_wamr.h"

struct WaitEventSet {
    int nevents;
    int nevents_space;
    WaitEvent *events;
    Latch *latch;
    int latch_pos;
    bool exit_on_postmaster_death;

#if defined(HAVE_SYS_EPOLL_H)
    int epoll_fd;
    struct epoll_event *epoll_ret_events;
#elif defined(WAIT_USE_KQUEUE)
    int kqueue_fd;
    struct kevent *kqueue_ret_events;
    bool report_postmaster_not_running;
#elif defined(WAIT_USE_POLL)
    struct pollfd *pollfds;
#elif defined(WAIT_USE_WIN32)
    HANDLE *handles;
#endif
};

typedef struct WaitEventSetEx {
    WaitEventSet *wait_set;
    int *slots;
    int head, tail, used;
} WaitEventSetEx;

WaitEventSetEx *
CreateWaitEventSetEx(MemoryContext context, int nevents) {
    WaitEventSetEx *rv;
    char *data;
    Size sz = 0;
    sz += MAXALIGN(sizeof(WaitEventSetEx));
    sz += sizeof(int) * nevents;

    data = (char *)MemoryContextAllocZero(context, sz);

    rv = (WaitEventSetEx *)data;
    data += MAXALIGN(sizeof(WaitEventSetEx));

    rv->slots = (int *)data;
    for (int i = 0; i < nevents; i++) {
        rv->slots[i] = i;
    }
    rv->head = rv->tail = rv->used = 0;
    rv->wait_set = CreateWaitEventSet(context, nevents);

    return rv;
}

int
AddWaitEventToSetEx(WaitEventSetEx *set,
                    uint32 events,
                    pgsocket fd,
                    Latch *latch,
                    void *user_data) {
    int rv;
    if (set->used == set->wait_set->nevents_space)
        return -1;
    set->wait_set->nevents = set->slots[set->head];
    rv = AddWaitEventToSet(set->wait_set, events, fd, latch, user_data);
    set->head = (set->head + 1) % set->wait_set->nevents_space;
    set->wait_set->nevents = ++set->used;
    return rv;
}

int
NextWaitEventPos(WaitEventSetEx *set) {
    if (set->used == set->wait_set->nevents_space)
        return -1;
    else
        return set->slots[set->head];
}

void
ModifyWaitEventEx(WaitEventSetEx *set, int pos, uint32 events, Latch *latch) {
    ModifyWaitEvent(set->wait_set, pos, events, latch);
}

void
DeleteWaitEventEx(WaitEventSetEx *set, int pos) {
    int rc;
    Assert(set->used > 0);
#if defined(HAVE_SYS_EPOLL_H)
    rc = epoll_ctl(set->wait_set->epoll_fd,
                   EPOLL_CTL_DEL,
                   set->wait_set->events[pos].fd,
                   NULL);
    if (rc < 0)
        ereport(ERROR,
                (errcode_for_socket_access(),
                 errmsg("%s() failed: %m", "epoll_ctl")));
#endif
    else {
        set->slots[set->tail] = pos;
        set->tail = (set->tail + 1) % set->wait_set->nevents_space;
        set->wait_set->nevents = --set->used;
    }
}

int
WaitEventSetWaitEx(WaitEventSetEx *set,
                   long timeout,
                   WaitEvent *occurred_events,
                   int nevents,
                   uint32 wait_event_info) {
    return WaitEventSetWait(set->wait_set,
                            timeout,
                            occurred_events,
                            nevents,
                            wait_event_info);
}

void
FreeWaitEventSetEx(WaitEventSetEx *set) {
    pfree(set->wait_set);
    pfree(set);
}
