#include <pwd.h>
#include <stddef.h>

extern "C" {

struct passwd *getpwuid(uid_t uid) {
    (void)uid;
    return NULL;
}

struct passwd *getpwnam(const char *name) {
    (void)name;
    return NULL;
}

int getpwuid_r(uid_t uid, struct passwd *pwd, char *buffer, size_t bufsize, struct passwd **result) {
    (void)uid; (void)pwd; (void)buffer; (void)bufsize;
    if (result) *result = NULL;
    return -1;
}

int getpwnam_r(const char *name, struct passwd *pwd, char *buffer, size_t bufsize, struct passwd **result) {
    (void)name; (void)pwd; (void)buffer; (void)bufsize;
    if (result) *result = NULL;
    return -1;
}

}
