// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stdint.hpp>

namespace lib {

using size_t = decltype(sizeof(0));
using ptrdiff_t = decltype((int*)0 - (int*)0);
using nullptr_t = decltype(nullptr);

} // namespace lib

using lib::nullptr_t;
using lib::ptrdiff_t;
using lib::size_t;
