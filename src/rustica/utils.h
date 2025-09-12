/*
 * SPDX-FileCopyrightText: 2024 燕几（北京）科技有限公司
 * SPDX-License-Identifier: Apache-2.0 OR MulanPSL-2.0
 */

#ifndef RUSTICA_UTILS_H
#define RUSTICA_UTILS_H

#include <sys/socket.h>

#define BACKEND_HELLO "RUSTICA!"

#define ERROR_BUF error_buf
#define ERROR_BUF_PARAMS ERROR_BUF, ERROR_BUF##_size
#define DECLARE_ERROR_BUF(size) \
    char ERROR_BUF[size];       \
    uint32 ERROR_BUF##_size = size;

typedef struct FDMessage {
    struct msghdr msg;
    struct cmsghdr *cmsg;
    char buf[CMSG_SPACE(sizeof(int))];
    struct iovec io;
    char byte;
} FDMessage;

void
rst_make_ipc_addr(struct sockaddr_un *addr);

#endif /* RUSTICA_UTILS_H */
