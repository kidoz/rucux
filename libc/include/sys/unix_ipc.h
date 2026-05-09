// SPDX-License-Identifier: MIT
#ifndef _SYS_UNIX_IPC_H
#define _SYS_UNIX_IPC_H

#include <uapi/kernel/unix_ipc.h>

#ifdef __cplusplus
extern "C" {
#endif

int rucux_unix_socket(const struct rucux_unix_ipc_socket_request* request);
int rucux_unix_bind(int sockfd, const struct rucux_unix_ipc_address* address);
int rucux_unix_listen(int sockfd, const struct rucux_unix_ipc_listen_request* request);
int rucux_unix_accept(int sockfd, struct rucux_unix_ipc_address* address);
int rucux_unix_connect(int sockfd, const struct rucux_unix_ipc_address* address);
int rucux_unix_socketpair(const struct rucux_unix_ipc_socket_request* request,
                          struct rucux_unix_ipc_pair_response* response);
int rucux_unix_shutdown(int sockfd, const struct rucux_unix_ipc_shutdown_request* request);

#ifdef __cplusplus
}
#endif

#endif // _SYS_UNIX_IPC_H
