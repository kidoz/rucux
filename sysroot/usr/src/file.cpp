// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {

// Extremely naive FILE wrapper
struct _FILE {
    int fd;
};

FILE* fopen(const char* filename, const char* mode) {
    int flags = 0;
    if (mode[0] == 'r') {
        flags = O_RDONLY;
    } else if (mode[0] == 'w') {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (mode[0] == 'a') {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    }

    int fd = open(filename, flags, 0666);
    if (fd < 0) return nullptr;

    return fdopen(fd, mode);
}

FILE* fdopen(int fd, const char* mode) {
    (void)mode;
    _FILE* f = (_FILE*)malloc(sizeof(_FILE));
    if (!f) return nullptr;
    f->fd = fd;
    return (FILE*)f;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (!stream) return 0;
    _FILE* f = (_FILE*)stream;

    size_t total = size * nmemb;
    if (total == 0) return 0;

    ssize_t bytes_read = read(f->fd, ptr, total);
    if (bytes_read <= 0) return 0;

    return bytes_read / size;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (!stream) return 0;
    _FILE* f = (_FILE*)stream;

    size_t total = size * nmemb;
    if (total == 0) return 0;

    ssize_t bytes_written = write(f->fd, ptr, total);
    if (bytes_written <= 0) return 0;

    return bytes_written / size;
}

int fclose(FILE* stream) {
    if (!stream) return -1;
    _FILE* f = (_FILE*)stream;
    int ret = close(f->fd);
    free(f);
    return ret;
}

// Stubs for zlib
int fseek(FILE* stream, long int offset, int whence) {
    (void)stream;
    (void)offset;
    (void)whence;
    return -1;
}

long int ftell(FILE* stream) {
    (void)stream;
    return -1;
}

int fflush(FILE* stream) {
    (void)stream;
    return 0;
}

int ferror(FILE* stream) {
    (void)stream;
    return 0;
}

int feof(FILE* stream) {
    (void)stream;
    return 0; // stub
}

char* fgets(char* s, int size, FILE* stream) {
    (void)s;
    (void)size;
    (void)stream;
    return nullptr; // stub
}

int fcntl(int fd, int cmd, ...) {
    (void)fd;
    (void)cmd;
    return 0; // stub
}

int stat(const char* pathname, struct stat* statbuf) {
    (void)pathname;
    if (statbuf) {
        statbuf->st_size = 0;
        statbuf->st_mode = 0;
    }
    return 0; // stub
}

int fstat(int fd, struct stat* statbuf) {
    (void)fd;
    if (statbuf) {
        statbuf->st_size = 0;
        statbuf->st_mode = 0;
    }
    return 0; // stub
}

} // extern "C"
