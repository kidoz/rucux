// SPDX-License-Identifier: MIT
#ifndef _LIBC_STDLIB_H
#define _LIBC_STDLIB_H

#include <stddef.h>
#include <stdint.h>
#include <locale.h>

#define MB_CUR_MAX 1

#ifdef __cplusplus
extern "C" {
#endif

long long strtoll_l(const char *nptr, char **endptr, int base, locale_t loc);
unsigned long long strtoull_l(const char *nptr, char **endptr, int base, locale_t loc);
long double strtold_l(const char *nptr, char **endptr, locale_t loc);
float strtof_l(const char *nptr, char **endptr, locale_t loc);
double strtod_l(const char *nptr, char **endptr, locale_t loc);

void abort(void) __attribute__((noreturn));

void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
int posix_memalign(void **memptr, size_t alignment, size_t size);
void exit(int status) __attribute__((noreturn));
const char* getprogname(void);

long strtol(const char* nptr, char** endptr, int base);
unsigned long strtoul(const char* nptr, char** endptr, int base);
long long strtoll(const char* nptr, char** endptr, int base);
unsigned long long strtoull(const char* nptr, char** endptr, int base);
double strtod(const char* nptr, char** endptr);

int atoi(const char* nptr);
long atol(const char* nptr);
long long atoll(const char* nptr);
double atof(const char* nptr);
float strtof(const char* nptr, char** endptr);
long double strtold(const char* nptr, char** endptr);

int abs(int j);
long labs(long j);
long long llabs(long long j);

typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;

div_t div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);
lldiv_t lldiv(long long numer, long long denom);

char* getenv(const char* name);
int putenv(char* string);
int setenv(const char* name, const char* value, int overwrite);
int unsetenv(const char* name);
char *realpath(const char *path, char *resolved_path);

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));

int atexit(void (*func)(void));
int at_quick_exit(void (*func)(void));
void quick_exit(int status) __attribute__((noreturn));
void _Exit(int status) __attribute__((noreturn));
int system(const char* command);

int mblen(const char* s, size_t n);
int mbtowc(int* pwc, const char* s, size_t n);
int wctomb(char* s, int wc);
size_t mbstowcs(int* pwcs, const char* s, size_t n);
size_t wcstombs(char* s, const int* pwcs, size_t n);

void* aligned_alloc(size_t alignment, size_t size);

uint32_t arc4random(void);
void arc4random_buf(void* buf, size_t nbytes);
uint32_t arc4random_uniform(uint32_t upper_bound);

int rand(void);
void srand(unsigned int seed);

long random(void);
void srandom(unsigned int seed);
void srand48(long int seedval);

#ifdef __cplusplus
}
#endif

#endif // _LIBC_STDLIB_H
