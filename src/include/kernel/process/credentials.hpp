// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <uapi/kernel/credentials.h>

namespace kernel::process {

using credentials = rucux_credentials;

constexpr uint32_t ROOT_UID = 0;
constexpr uint32_t ROOT_GID = 0;
constexpr uint32_t ROOT_CAPABILITIES =
    RUCUX_CAP_SPAWN_AS | RUCUX_CAP_DISPLAY_ADMIN | RUCUX_CAP_INPUT_ADMIN | RUCUX_CAP_SESSION_ADMIN | RUCUX_CAP_POWER;

credentials root_credentials(uint32_t sid = 0, uint32_t pgid = 0) noexcept;

long sys_getuid() noexcept;
long sys_geteuid() noexcept;
long sys_getgid() noexcept;
long sys_getegid() noexcept;
long sys_setuid(uint32_t uid) noexcept;
long sys_seteuid(uint32_t uid) noexcept;
long sys_setgid(uint32_t gid) noexcept;
long sys_setegid(uint32_t gid) noexcept;
long sys_getsid(int32_t pid) noexcept;
long sys_setsid() noexcept;
long sys_getpgid(int32_t pid) noexcept;
long sys_setpgid(int32_t pid, uint32_t pgid) noexcept;

} // namespace kernel::process
