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

FILE* freopen(const char* pathname, const char* mode, FILE* stream) {
    if (stream) fclose(stream);
    return fopen(pathname, mode);
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

int fgetc(FILE* stream) {
    unsigned char c;
    if (fread(&c, 1, 1, stream) != 1) return EOF;
    return c;
}

int fputc(int c, FILE* stream) {
    unsigned char ch = (unsigned char)c;
    if (fwrite(&ch, 1, 1, stream) != 1) return EOF;
    return ch;
}

int fputs(const char* s, FILE* stream) {
    if (!s || !stream) return EOF;
    size_t len = 0;
    while (s[len]) len++;
    if (fwrite(s, 1, len, stream) != len) return EOF;
    return 0;
}

int getc(FILE* stream) {
    return fgetc(stream);
}

int getchar(void) {
    return fgetc(stdin);
}

int ungetc(int c, FILE* stream) {
    (void)c;
    (void)stream;
    return EOF; // stub
}

int rename(const char* oldpath, const char* newpath) {
    (void)oldpath; (void)newpath;
    return -1; // stub
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size) {
    (void)stream; (void)buf; (void)mode; (void)size;
    return 0; // stub
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

int mkdir(const char *pathname, mode_t mode) {
    (void)pathname; (void)mode;
    return 0; // stub
}

} // extern "C"
