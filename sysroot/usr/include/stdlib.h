// SPDX-License-Identifier: MIT
#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void exit(int status) __attribute__((noreturn));
const char* getprogname(void);

long strtol(const char* nptr, char** endptr, int base);
unsigned long strtoul(const char* nptr, char** endptr, int base);
long long strtoll(const char* nptr, char** endptr, int base);
unsigned long long strtoull(const char* nptr, char** endptr, int base);

int atoi(const char* nptr);

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*));

uint32_t arc4random(void);
void arc4random_buf(void* buf, size_t nbytes);
uint32_t arc4random_uniform(uint32_t upper_bound);

#ifdef __cplusplus
}
#endif

#endif // _STDLIB_H
