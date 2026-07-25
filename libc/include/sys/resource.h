// SPDX-License-Identifier: MIT
#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

#define RLIMIT_NOFILE 7
#define RLIMIT_AS 9

#define RLIM_INFINITY (~0UL)

struct rlimit {
    unsigned long rlim_cur;
    unsigned long rlim_max;
};

int getrlimit(int resource, struct rlimit* rlim);
int setrlimit(int resource, const struct rlimit* rlim);

#ifdef __cplusplus
}
#endif

#endif // _SYS_RESOURCE_H
