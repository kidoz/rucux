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

inline bool has_capability(const credentials& creds, uint32_t cap) noexcept {
    return cap != 0 && (creds.capabilities & cap) == cap;
}

// A thread may signal itself, a privileged caller may signal anyone, and any
// caller may signal a thread with the same effective uid.
inline bool may_signal(const credentials& caller, uint32_t caller_tid, const credentials& target,
                       uint32_t target_tid) noexcept {
    if (caller_tid == target_tid) return true;
    if (caller.euid == ROOT_UID) return true;
    return caller.euid == target.ruid || caller.euid == target.euid;
}

// Root may address anyone. Anyone may address a root server. Otherwise the
// endpoints must share an effective uid.
inline bool may_ipc(const credentials& caller, const credentials& target) noexcept {
    return caller.euid == ROOT_UID || target.euid == ROOT_UID || caller.euid == target.euid;
}

credentials root_credentials(uint32_t sid = 0, uint32_t pgid = 0) noexcept;

bool raw_io_allowed() noexcept;
bool ipc_send_allowed(uint32_t target_tid) noexcept;

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
