// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <sys/types.h>
#include <uapi/kernel/trace.h>

#ifdef __cplusplus
extern "C" {
#endif

int trace_reset(void);
int trace_enable(void);
int trace_disable(void);
ssize_t trace_snapshot(void* buffer, size_t size);
int trace_get_stats(struct trace_stats* stats);

#ifdef __cplusplus
}
#endif
