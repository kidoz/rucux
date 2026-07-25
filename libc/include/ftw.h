#ifndef _FTW_H
#define _FTW_H
#ifdef __cplusplus
extern "C" {
#endif
#define FTW_F 1
#define FTW_D 2
#define FTW_DNR 3
#define FTW_DP 4
#define FTW_NS 5
#define FTW_SL 6
#define FTW_SLN 7
#define FTW_PHYS 1
#define FTW_MOUNT 2
#define FTW_CHDIR 4
#define FTW_DEPTH 8
#define FTW_ACTIONRETVAL 16
#define FTW_CONTINUE 0
#define FTW_STOP 1
#define FTW_SKIP_SUBTREE 2
#define FTW_SKIP_SIBLINGS 3
struct stat;
typedef int (*__ftw_func_t)(const char*, const struct stat*, int);
typedef int (*__nftw_func_t)(const char*, const struct stat*, int, struct FTW*);
struct FTW {
    int base;
    int level;
};
int ftw(const char* dirpath, __ftw_func_t fn, int nopenfd);
int nftw(const char* dirpath, __nftw_func_t fn, int nopenfd, int flags);
#ifdef __cplusplus
}
#endif
#endif
