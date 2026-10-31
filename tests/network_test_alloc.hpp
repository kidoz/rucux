// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>

// Force-included only for production network TUs in hosted builds. Redirect
// their allocation symbols, leaving libc++, sanitizers and libFuzzer on their
// own matching allocators (Darwin dylibs may bind new/delete independently).
#define RUCUX_STRINGIFY_IMPL(value) #value
#define RUCUX_STRINGIFY(value) RUCUX_STRINGIFY_IMPL(value)
#define RUCUX_ALLOC_SYMBOL(name) __asm__(RUCUX_STRINGIFY(__USER_LABEL_PREFIX__) name)

void* operator new(size_t) RUCUX_ALLOC_SYMBOL("rucux_network_allocate");
void* operator new[](size_t) RUCUX_ALLOC_SYMBOL("rucux_network_allocate");
void operator delete(void*) noexcept RUCUX_ALLOC_SYMBOL("rucux_network_deallocate");
void operator delete[](void*) noexcept RUCUX_ALLOC_SYMBOL("rucux_network_deallocate");
void operator delete(void*, size_t) noexcept RUCUX_ALLOC_SYMBOL("rucux_network_deallocate_sized");
void operator delete[](void*, size_t) noexcept RUCUX_ALLOC_SYMBOL("rucux_network_deallocate_sized");

#undef RUCUX_ALLOC_SYMBOL
#undef RUCUX_STRINGIFY
#undef RUCUX_STRINGIFY_IMPL
