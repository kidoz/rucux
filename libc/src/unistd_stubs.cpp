// SPDX-License-Identifier: MIT
#include <unistd.h>
#include <stdio.h>
#include <string.h>

extern "C" {

void perror(const char *s) {
    if (s && *s) {
        printf("%s: Unknown error\n", s);
    } else {
        printf("Unknown error\n");
    }
}

int unlink(const char *pathname) {
    (void)pathname;
    return -1; // stub
}

int fileno(FILE *stream) {
    if (!stream) return -1;
    // We cast it back to our internal struct
    struct _FILE { int fd; };
    return ((_FILE*)stream)->fd;
}

uid_t getuid(void) {
    return 0; // stub
}

int isatty(int fd) {
    (void)fd;
    return 1; // Everything is a TTY in rucux!
}

unsigned int sleep(unsigned int seconds) {
    (void)seconds;
    return 0; // stub
}

int gethostname(char *name, size_t len) {
    if (!name || len < 6) return -1;
    strcpy(name, "rucux");
    return 0;
}

int access(const char *pathname, int mode) {
    (void)pathname; (void)mode;
    return 0; // stub
}

long sysconf(int name) {
    (void)name;
    return 4096; // 4KB pages
}

}
