// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stdint.hpp>

namespace lib {

using size_t = decltype(sizeof(0));
using ptrdiff_t = int64_t;
using nullptr_t = decltype(nullptr);

} // namespace lib

using lib::nullptr_t;
using lib::ptrdiff_t;
using lib::size_t;
