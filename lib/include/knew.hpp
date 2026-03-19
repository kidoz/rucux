// SPDX-License-Identifier: MIT
#pragma once
#include <lib/new.hpp>

void* operator new(size_t size);
void* operator new[](size_t size);
void operator delete(void* p) noexcept;
void operator delete[](void* p) noexcept;
void operator delete(void* p, size_t) noexcept;
void operator delete[](void* p, size_t) noexcept;
