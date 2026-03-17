// SPDX-License-Identifier: MIT
#ifndef _LIBC_STDIO_H
#define _LIBC_STDIO_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _FILE FILE;

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

typedef long fpos_t;

int printf(const char* format, ...);
int puts(const char* s);
int fprintf(FILE* stream, const char* format, ...);
int dprintf(int fd, const char* format, ...);
int sprintf(char* str, const char* format, ...);
int vfscanf(FILE* stream, const char* format, va_list arg);
int vsscanf(const char* s, const char* format, va_list arg);
int fgetpos(FILE* stream, fpos_t* pos);
int fsetpos(FILE* stream, const fpos_t* pos);
int remove(const char* filename);
int rename(const char* oldname, const char* newname);
FILE* tmpfile(void);
char* tmpnam(char* s);
int scanf(const char* format, ...);
int vscanf(const char* format, va_list arg);
int vprintf(const char* format, va_list arg);
int fscanf(FILE* stream, const char* format, ...);
int sscanf(const char* s, const char* format, ...);
int snprintf(char* str, size_t size, const char* format, ...);
int vsnprintf(char* str, size_t size, const char* format, va_list ap);
int vsprintf(char* str, const char* format, va_list ap);
int vfprintf(FILE* stream, const char* format, va_list ap);
int vasprintf(char **strp, const char *fmt, va_list ap);

FILE* fopen(const char* pathname, const char* mode);
FILE* fdopen(int fd, const char* mode);
FILE* freopen(const char* pathname, const char* mode, FILE* stream);
int fclose(FILE* stream);
int fileno(FILE* stream);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
int fseek(FILE* stream, long offset, int whence);
long ftell(FILE* stream);
void rewind(FILE* stream);

int fseeko(FILE* stream, long offset, int whence);
long ftello(FILE* stream);

int fflush(FILE* stream);
int ferror(FILE* stream);
int feof(FILE* stream);
void clearerr(FILE* stream);

int fgetc(FILE* stream);
char* fgets(char* s, int size, FILE* stream);
int fputc(int c, FILE* stream);
int fputs(const char* s, FILE* stream);
int getc(FILE* stream);
int getchar(void);
int putc(int c, FILE* stream);
int putchar(int c);
int ungetc(int c, FILE* stream);

int rename(const char* oldpath, const char* newpath);

void perror(const char* s);

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

int setvbuf(FILE *stream, char *buf, int mode, size_t size);
void setbuf(FILE* stream, char* buf);

#define BUFSIZ 1024
#define EOF (-1)

#ifdef __cplusplus
}
#endif

#endif // _LIBC_STDIO_H
