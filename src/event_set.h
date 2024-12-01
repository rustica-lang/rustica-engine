/*
 * Copyright (c) 2024 燕几（北京）科技有限公司
 *
 * Rustica (runtime) is licensed under Mulan PSL v2. You can use this
 * software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *              http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
 * OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the Mulan PSL v2 for more details.
 */

#ifndef RUSTICA_EVENT_SET_H
#define RUSTICA_EVENT_SET_H

#include "postgres.h"
#include "storage/latch.h"

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

#endif /* RUSTICA_EVENT_SET_H */
