/*
 * Copyright (c) 2024 燕几（北京）科技有限公司
 * Rustica (runtime) is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan
 * PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <string.h>
#include "postgres.h"
#include "utils/guc.h"
#include "rustica_wamr.h"

char *rst_listen_addresses = NULL;
int rst_port = 8080;
int rst_worker_idle_timeout = 60;
char *rst_database = NULL;

void
rst_init_gucs() {
    DefineCustomStringVariable(
        "rustica.listen_addresses",
        "Sets the host name or IP address(es) to listen to.",
        "Default is 'localhost'.",
        &rst_listen_addresses,
        "localhost",
        PGC_USERSET,
        0,
        NULL,
        NULL,
        NULL);
    DefineCustomIntVariable("rustica.port",
                            "Sets the TCP port the server listens on.",
                            "Default is 8080.",
                            &rst_port,
                            8080,
                            1,
                            65535,
                            PGC_USERSET,
                            0,
                            NULL,
                            NULL,
                            NULL);
    DefineCustomIntVariable("rustica.worker_idle_timeout",
                            "Sets the worker idle timeout in seconds.",
                            "Default is 60; 0 for no timeout.",
                            &rst_worker_idle_timeout,
                            60,
                            0,
                            3600 * 24 * 20, // under 32-bit long in ms
                            PGC_USERSET,
                            0,
                            NULL,
                            NULL,
                            NULL);
    DefineCustomStringVariable("rustica.database",
                               "Sets the database that is used by Rustica.",
                               "Only one database allowed.",
                               &rst_database,
                               NULL,
                               PGC_USERSET,
                               0,
                               NULL,
                               NULL,
                               NULL);
}
