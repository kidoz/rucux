// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

// Fixed-width wire format shared by libc and the kernel on every target.
struct rucux_stat_time {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t __reserved;
    uint64_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    int64_t st_size;
    struct rucux_stat_time st_atim;
    struct rucux_stat_time st_mtim;
    struct rucux_stat_time st_ctim;
    int64_t st_blksize;
    int64_t st_blocks;
};

#ifdef __cplusplus
static_assert(sizeof(struct stat) == 120);
static_assert(__builtin_offsetof(struct stat, st_size) == 48);
static_assert(__builtin_offsetof(struct stat, st_blocks) == 112);
#endif
