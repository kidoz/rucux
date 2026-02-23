// SPDX-License-Identifier: MIT
#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int printf(const char* format, ...);
int puts(const char* s);
int putchar(int c);

extern void* stderr;
#define fprintf(stream, ...) printf(__VA_ARGS__)

typedef void FILE;

int fputs(const char* s, FILE* stream);

extern FILE* fopen(const char* filename, const char* mode);
extern FILE* fdopen(int fd, const char* mode);
extern size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
extern size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
extern int fclose(FILE* stream);
extern int fseek(FILE* stream, long int offset, int whence);
extern long int ftell(FILE* stream);
extern int fflush(FILE* stream);
extern int ferror(FILE* stream);
extern void clearerr(FILE* stream);
extern int feof(FILE* stream);
extern char* fgets(char* s, int size, FILE* stream);

extern int snprintf(char* str, size_t size, const char* format, ...);
extern int vsnprintf(char* str, size_t size, const char* format, void* ap);
extern int vsprintf(char* str, const char* format, void* ap);
extern int sprintf(char* str, const char* format, ...);
extern int sscanf(const char* str, const char* format, ...);
extern int vfprintf(FILE* stream, const char* format, void* ap);

extern int fseeko(FILE* stream, off_t offset, int whence);
extern int fseeko64(FILE* stream, off64_t offset, int whence);
extern off_t ftello(FILE* stream);
extern off64_t ftello64(FILE* stream);

extern void perror(const char* s);
extern int fileno(FILE* stream);

extern FILE* stdin;
extern FILE* stdout;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)
#define BUFSIZ 1024

#ifdef __cplusplus
}
#endif

#endif // _STDIO_H
