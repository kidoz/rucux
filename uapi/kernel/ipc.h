// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

// SYS_IPC_SEND/RECV. sender is supplied by the kernel on receive; send ignores it.
struct rucux_ipc_message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};

// SYS_IPC_CALL: target TID + in/out words; SYS_IPC_WAIT: output words;
// SYS_IPC_REPLY: input words. One accepted call per server thread. A cancelled
// accepted call makes WAIT return -ECANCELED until REPLY acknowledges it with
// -ECANCELED. No new call is accepted before that acknowledgement.
struct rucux_ipc_words {
    uint64_t type;
    uint64_t d0;
    uint64_t d1;
    uint64_t d2;
};

#ifdef __cplusplus
static_assert(sizeof(rucux_ipc_message) == 40);
static_assert(sizeof(rucux_ipc_words) == 32);
#endif
