// SPDX-FileCopyrightText: 2024 燕几（北京）科技有限公司
// SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0

#ifndef RUSTICA_GUCS_H
#define RUSTICA_GUCS_H

extern char *rst_listen_addresses;
extern int rst_port;
extern int rst_worker_idle_timeout;
extern char *rst_database;

void
rst_init_gucs();

#endif /* RUSTICA_GUCS_H */
