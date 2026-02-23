// SPDX-License-Identifier: MIT
#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
void* memmove(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
void* memchr(const void* s, int c, size_t n);

char* strcpy(char* dest, const char* src);
int strcmp(const char* s1, const char* s2);
size_t strlen(const char* s);
char* strerror(int errnum);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strdup(const char* s);
char* strndup(const char* s, size_t n);
int strncmp(const char* s1, const char* s2, size_t n);
size_t strcspn(const char* s, const char* reject);
size_t strspn(const char* s, const char* accept);
char* strstr(const char* haystack, const char* needle);

#ifdef __cplusplus
}
#endif

#endif // _STRING_H
