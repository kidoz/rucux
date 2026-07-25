// SPDX-License-Identifier: MIT
#ifndef _DLFCN_H
#define _DLFCN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* dli_fname;
    void* dli_fbase;
    const char* dli_sname;
    void* dli_saddr;
} Dl_info;

static inline int dladdr(const void* addr, Dl_info* info) {
    (void)addr;
    (void)info;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif // _DLFCN_H