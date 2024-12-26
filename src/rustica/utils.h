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
