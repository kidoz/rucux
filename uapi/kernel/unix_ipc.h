// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#define RUCUX_UNIX_IPC_PROTOCOL_VERSION 1u
#define RUCUX_UNIX_IPC_PATH_MAX 108u

enum rucux_unix_ipc_status : uint32_t {
    RUCUX_UNIX_IPC_OK = 0,
    RUCUX_UNIX_IPC_ERR_INVALID = 1,
    RUCUX_UNIX_IPC_ERR_UNSUPPORTED = 2,
    RUCUX_UNIX_IPC_ERR_NOENT = 3,
    RUCUX_UNIX_IPC_ERR_BUSY = 4,
    RUCUX_UNIX_IPC_ERR_NOSPC = 5,
};

enum rucux_unix_ipc_socket_type : uint32_t {
    RUCUX_UNIX_IPC_SOCK_STREAM = 1,
    RUCUX_UNIX_IPC_SOCK_DGRAM = 2,
    RUCUX_UNIX_IPC_SOCK_SEQPACKET = 3,
};

enum rucux_unix_ipc_namespace_kind : uint16_t {
    RUCUX_UNIX_IPC_NAMESPACE_FILESYSTEM = 1,
    RUCUX_UNIX_IPC_NAMESPACE_ABSTRACT = 2,
};

enum rucux_unix_ipc_op : uint32_t {
    RUCUX_UNIX_IPC_OP_SOCKET = 1,
    RUCUX_UNIX_IPC_OP_BIND = 2,
    RUCUX_UNIX_IPC_OP_LISTEN = 3,
    RUCUX_UNIX_IPC_OP_ACCEPT = 4,
    RUCUX_UNIX_IPC_OP_CONNECT = 5,
    RUCUX_UNIX_IPC_OP_SOCKETPAIR = 6,
    RUCUX_UNIX_IPC_OP_SENDMSG = 7,
    RUCUX_UNIX_IPC_OP_RECVMSG = 8,
    RUCUX_UNIX_IPC_OP_SHUTDOWN = 9,
};

struct rucux_unix_ipc_address {
    uint16_t family;
    uint16_t namespace_kind;
    uint16_t path_length;
    char path[RUCUX_UNIX_IPC_PATH_MAX];
};

struct rucux_unix_ipc_socket_request {
    uint32_t type;
    uint32_t flags;
};

struct rucux_unix_ipc_listen_request {
    uint32_t backlog;
    uint32_t reserved;
};

struct rucux_unix_ipc_shutdown_request {
    uint32_t how;
    uint32_t reserved;
};

struct rucux_unix_ipc_pair_response {
    int32_t first_fd;
    int32_t second_fd;
};

struct rucux_unix_ipc_result {
    int32_t value;
    uint32_t status;
};
