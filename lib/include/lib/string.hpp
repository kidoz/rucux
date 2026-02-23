// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>

namespace lib {

extern "C" {

void* memcpy(void* __restrict dest, const void* __restrict src, size_t n) noexcept;
void* memset(void* s, int c, size_t n) noexcept;
void* memmove(void* dest, const void* src, size_t n) noexcept;
int memcmp(const void* s1, const void* s2, size_t n) noexcept;

char* strcpy(char* dest, const char* src) noexcept;
int strcmp(const char* s1, const char* s2) noexcept;

} // extern "C"

} // namespace lib
