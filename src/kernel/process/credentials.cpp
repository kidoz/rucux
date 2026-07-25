// SPDX-License-Identifier: MIT
#include <kernel/process/credentials.hpp>
#include <kernel/scheduler/scheduler.hpp>

namespace kernel::process {

credentials root_credentials(uint32_t sid, uint32_t pgid) noexcept {
    return {
        ROOT_UID, ROOT_UID, ROOT_UID, ROOT_GID, ROOT_GID, ROOT_GID, sid, pgid, ROOT_CAPABILITIES,
    };
}

static scheduler::thread* current() noexcept {
    return scheduler::scheduler::current_thread();
}

static bool is_privileged(const credentials& creds) noexcept {
    return creds.euid == ROOT_UID;
}

long sys_getuid() noexcept {
    auto* thread = current();
    return thread ? static_cast<long>(thread->creds.ruid) : -1;
}

long sys_geteuid() noexcept {
    auto* thread = current();
    return thread ? static_cast<long>(thread->creds.euid) : -1;
}

long sys_getgid() noexcept {
    auto* thread = current();
    return thread ? static_cast<long>(thread->creds.rgid) : -1;
}

long sys_getegid() noexcept {
    auto* thread = current();
    return thread ? static_cast<long>(thread->creds.egid) : -1;
}

long sys_setuid(uint32_t uid) noexcept {
    auto* thread = current();
    if (!thread) return -1;

    if (is_privileged(thread->creds)) {
        thread->creds.ruid = uid;
        thread->creds.euid = uid;
        thread->creds.suid = uid;
        if (uid != ROOT_UID) {
            thread->creds.capabilities = 0;
        }
        return 0;
    }

    if (uid == thread->creds.ruid || uid == thread->creds.suid) {
        thread->creds.euid = uid;
        return 0;
    }
    return -1;
}

long sys_seteuid(uint32_t uid) noexcept {
    auto* thread = current();
    if (!thread) return -1;

    if (is_privileged(thread->creds) || uid == thread->creds.ruid || uid == thread->creds.suid) {
        thread->creds.euid = uid;
        if (uid != ROOT_UID) {
            thread->creds.capabilities = 0;
        }
        return 0;
    }
    return -1;
}

long sys_setgid(uint32_t gid) noexcept {
    auto* thread = current();
    if (!thread) return -1;

    if (is_privileged(thread->creds)) {
        thread->creds.rgid = gid;
        thread->creds.egid = gid;
        thread->creds.sgid = gid;
        return 0;
    }

    if (gid == thread->creds.rgid || gid == thread->creds.sgid) {
        thread->creds.egid = gid;
        return 0;
    }
    return -1;
}

long sys_setegid(uint32_t gid) noexcept {
    auto* thread = current();
    if (!thread) return -1;

    if (is_privileged(thread->creds) || gid == thread->creds.rgid || gid == thread->creds.sgid) {
        thread->creds.egid = gid;
        return 0;
    }
    return -1;
}

long sys_getsid(int32_t pid) noexcept {
    scheduler::thread* thread = nullptr;
    if (pid == 0) {
        thread = current();
    } else if (pid > 0) {
        thread = scheduler::scheduler::get_thread_by_tid(static_cast<uint32_t>(pid));
    }
    return thread ? static_cast<long>(thread->creds.sid) : -1;
}

long sys_setsid() noexcept {
    auto* thread = current();
    if (!thread || thread->creds.pgid == thread->tid) return -1;

    thread->creds.sid = thread->tid;
    thread->creds.pgid = thread->tid;
    return static_cast<long>(thread->creds.sid);
}

long sys_getpgid(int32_t pid) noexcept {
    scheduler::thread* thread = nullptr;
    if (pid == 0) {
        thread = current();
    } else if (pid > 0) {
        thread = scheduler::scheduler::get_thread_by_tid(static_cast<uint32_t>(pid));
    }
    return thread ? static_cast<long>(thread->creds.pgid) : -1;
}

long sys_setpgid(int32_t pid, uint32_t pgid) noexcept {
    auto* caller = current();
    if (!caller) return -1;

    scheduler::thread* target = nullptr;
    if (pid == 0 || static_cast<uint32_t>(pid) == caller->tid) {
        target = caller;
    } else if (pid > 0 && is_privileged(caller->creds)) {
        target = scheduler::scheduler::get_thread_by_tid(static_cast<uint32_t>(pid));
    }

    if (!target || target->creds.sid != caller->creds.sid) return -1;
    target->creds.pgid = pgid == 0 ? target->tid : pgid;
    return 0;
}

} // namespace kernel::process
