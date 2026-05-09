// SPDX-License-Identifier: MIT
#include <sys/unix_ipc.h>
#include <uapi/kernel/syscalls.h>

#include "syscall_impl.h"

static long unix_ipc_call(long op, long arg0 = 0, long arg1 = 0, long arg2 = 0, long arg3 = 0,
                          long arg4 = 0) {
    return __syscall(SYS_UNIX_IPC, op, arg0, arg1, arg2, arg3, arg4);
}

extern "C" {

int rucux_unix_socket(const struct rucux_unix_ipc_socket_request* request) {
    return (int)unix_ipc_call(RUCUX_UNIX_IPC_OP_SOCKET, (long)request);
}

int rucux_unix_bind(int sockfd, const struct rucux_unix_ipc_address* address) {
    return (int)unix_ipc_call(RUCUX_UNIX_IPC_OP_BIND, (long)sockfd, (long)address);
}

int rucux_unix_listen(int sockfd, const struct rucux_unix_ipc_listen_request* request) {
    return (int)unix_ipc_call(RUCUX_UNIX_IPC_OP_LISTEN, (long)sockfd, (long)request);
}

int rucux_unix_accept(int sockfd, struct rucux_unix_ipc_address* address) {
    return (int)unix_ipc_call(RUCUX_UNIX_IPC_OP_ACCEPT, (long)sockfd, (long)address);
}

int rucux_unix_connect(int sockfd, const struct rucux_unix_ipc_address* address) {
    return (int)unix_ipc_call(RUCUX_UNIX_IPC_OP_CONNECT, (long)sockfd, (long)address);
}

int rucux_unix_socketpair(const struct rucux_unix_ipc_socket_request* request,
                          struct rucux_unix_ipc_pair_response* response) {
    return (int)unix_ipc_call(RUCUX_UNIX_IPC_OP_SOCKETPAIR, (long)request, (long)response);
}

int rucux_unix_shutdown(int sockfd, const struct rucux_unix_ipc_shutdown_request* request) {
    return (int)unix_ipc_call(RUCUX_UNIX_IPC_OP_SHUTDOWN, (long)sockfd, (long)request);
}

} // extern "C"
