// SPDX-License-Identifier: MIT
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <uapi/kernel/initd.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

#include "config.hpp"

extern "C" {

struct message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};
}

namespace {

constexpr int MAX_SERVICES = 16;
constexpr int MAX_SERVICE_DEPS = 3;

struct service_entry {
    char name[INITD_SERVICE_NAME_MAX];
    const char* path;
    uint32_t tid;
    uint32_t state;
    bool autostart;
    bool restart_on_failure;
    uint32_t max_restart_attempts;
    uint32_t restart_delay_ms;
    bool registered;
    bool stop_requested;
    bool restart_pending;
    uint32_t restart_generation;
    uint32_t failure_count;
    uint32_t restart_count;
    uint32_t restart_budget_used;
    uint32_t last_exit_status;
    const char* dependencies[MAX_SERVICE_DEPS];
    uint32_t dependency_count;
    bool start_requested;
};

service_entry g_registry[MAX_SERVICES] = {};
int g_service_count = 0;
pthread_mutex_t g_services_lock = PTHREAD_MUTEX_INITIALIZER;

void copy_name(char* dst, const char* src) {
    int i = 0;
    for (; i < INITD_SERVICE_NAME_MAX - 1 && src[i]; ++i) {
        dst[i] = src[i];
    }
    for (; i < INITD_SERVICE_NAME_MAX; ++i) {
        dst[i] = '\0';
    }
}

void decode_name(char* dst, const message& msg) {
    const char* src = reinterpret_cast<const char*>(msg.data);
    copy_name(dst, src);
}

bool names_equal(const char* lhs, const char* rhs) {
    for (int i = 0; i < INITD_SERVICE_NAME_MAX; ++i) {
        if (lhs[i] != rhs[i]) return false;
        if (lhs[i] == '\0') return true;
    }
    return true;
}

service_entry* find_service_locked(const char* name) {
    for (int i = 0; i < g_service_count; ++i) {
        if (names_equal(g_registry[i].name, name)) return &g_registry[i];
    }
    return nullptr;
}

service_entry* find_service_by_tid_locked(uint32_t tid) {
    for (int i = 0; i < g_service_count; ++i) {
        if (g_registry[i].tid == tid) return &g_registry[i];
    }
    return nullptr;
}

service_entry* ensure_service_locked(const char* name) {
    auto* entry = find_service_locked(name);
    if (entry) return entry;
    if (g_service_count >= MAX_SERVICES) return nullptr;

    entry = &g_registry[g_service_count++];
    copy_name(entry->name, name);
    entry->path = nullptr;
    entry->tid = 0;
    entry->state = INITD_SERVICE_UNKNOWN;
    entry->autostart = false;
    entry->restart_on_failure = false;
    entry->max_restart_attempts = 0;
    entry->restart_delay_ms = 0;
    entry->registered = false;
    entry->stop_requested = false;
    entry->restart_pending = false;
    entry->restart_generation = 0;
    entry->failure_count = 0;
    entry->restart_count = 0;
    entry->restart_budget_used = 0;
    entry->last_exit_status = 0;
    for (int i = 0; i < MAX_SERVICE_DEPS; ++i)
        entry->dependencies[i] = nullptr;
    entry->dependency_count = 0;
    entry->start_requested = false;
    return entry;
}

void reply_lookup(uint32_t target, uint32_t tid) {
    message resp = {};
    resp.type = INITD_MSG_LOOKUP_SERVICE;
    resp.data[0] = tid;
    syscall(SYS_IPC_SEND, target, reinterpret_cast<long>(&resp));
}

void reply_control(uint32_t target, uint32_t result, uint32_t tid, uint32_t state, uint64_t meta = 0) {
    message resp = {};
    resp.type = INITD_MSG_CONTROL_REPLY;
    resp.data[0] = result;
    resp.data[1] = tid;
    resp.data[2] = state;
    resp.data[3] = meta;
    syscall(SYS_IPC_SEND, target, reinterpret_cast<long>(&resp));
}

uint64_t pack_service_meta(const service_entry* entry) {
    if (!entry) return 0;
    return ((static_cast<uint64_t>(entry->failure_count) & 0xffffULL) << 48) |
           ((static_cast<uint64_t>(entry->restart_count) & 0xffffULL) << 32) |
           static_cast<uint64_t>(entry->last_exit_status);
}

long spawn_service_locked(service_entry* entry);
enum class start_result { STARTED, DEFERRED, FAILED };
start_result request_service_start_locked(service_entry* entry) noexcept;
void kick_pending_services_locked() noexcept;

struct restart_request {
    service_entry* entry;
    uint32_t generation;
};

void* restart_worker_main(void* arg) {
    auto* req = static_cast<restart_request*>(arg);
    if (!req || !req->entry) return nullptr;

    service_entry* entry = req->entry;
    uint32_t generation = req->generation;
    uint32_t delay_ms = entry->restart_delay_ms;
    free(req);

    if (delay_ms > 0) {
        usleep(delay_ms * 1000U);
    }

    pthread_mutex_lock(&g_services_lock);
    if (!entry->restart_pending || entry->restart_generation != generation || entry->tid != 0 ||
        entry->stop_requested) {
        pthread_mutex_unlock(&g_services_lock);
        return nullptr;
    }

    entry->restart_pending = false;
    entry->start_requested = true;
    start_result result = request_service_start_locked(entry);
    if (result == start_result::STARTED) {
        uint32_t new_tid = entry->tid;
        pthread_mutex_unlock(&g_services_lock);
        printf("init: restarted %s as tid %u\n", entry->name, new_tid);
        return nullptr;
    }
    if (result == start_result::DEFERRED) {
        pthread_mutex_unlock(&g_services_lock);
        printf("init: delayed restart of %s until dependencies are ready\n", entry->name);
        return nullptr;
    }
    pthread_mutex_unlock(&g_services_lock);
    printf("init: restart spawn failed for %s\n", entry->name);
    return nullptr;
}

void schedule_restart_locked(service_entry* entry) {
    if (!entry || !entry->restart_on_failure) return;
    if (entry->restart_pending || entry->tid != 0) return;
    if (entry->max_restart_attempts != 0 && entry->restart_budget_used >= entry->max_restart_attempts) return;

    auto* req = static_cast<restart_request*>(malloc(sizeof(restart_request)));
    if (!req) return;
    req->entry = entry;
    req->generation = ++entry->restart_generation;

    entry->restart_pending = true;
    entry->restart_budget_used++;
    entry->restart_count++;

    pthread_t restart_thread = 0;
    if (pthread_create(&restart_thread, nullptr, restart_worker_main, req) != 0) {
        entry->restart_pending = false;
        free(req);
    }
}

bool dependencies_ready_locked(const service_entry* entry, const char** missing_dep = nullptr) {
    if (!entry) return false;

    for (uint32_t i = 0; i < entry->dependency_count; ++i) {
        const char* dep_name = entry->dependencies[i];
        if (!dep_name) continue;

        auto* dep = find_service_locked(dep_name);
        if (!dep || dep->state != INITD_SERVICE_RUNNING || dep->tid == 0) {
            if (missing_dep) *missing_dep = dep_name;
            return false;
        }
    }

    return true;
}

long spawn_service_locked(service_entry* entry) {
    if (!entry || !entry->path) return -1;

    long tid = syscall(SYS_SPAWN, reinterpret_cast<long>(entry->path));
    if (tid < 0) {
        entry->tid = 0;
        entry->registered = false;
        entry->stop_requested = false;
        entry->state = INITD_SERVICE_FAILED;
        return -1;
    }

    entry->tid = static_cast<uint32_t>(tid);
    entry->registered = false;
    entry->stop_requested = false;
    entry->start_requested = false;
    entry->state = INITD_SERVICE_STARTING;
    return tid;
}

start_result request_service_start_locked(service_entry* entry) noexcept {
    if (!entry || !entry->path) return start_result::FAILED;
    if (entry->tid != 0) return start_result::STARTED;

    const char* missing_dep = nullptr;
    if (!dependencies_ready_locked(entry, &missing_dep)) {
        entry->state = INITD_SERVICE_WAITING;
        return start_result::DEFERRED;
    }

    long tid = spawn_service_locked(entry);
    if (tid < 0) return start_result::FAILED;
    return start_result::STARTED;
}

void kick_pending_services_locked() noexcept {
    for (int i = 0; i < g_service_count; ++i) {
        auto* entry = &g_registry[i];
        if (!entry->start_requested || entry->tid != 0 || entry->stop_requested) continue;
        if (entry->state != INITD_SERVICE_WAITING && entry->state != INITD_SERVICE_STOPPED &&
            entry->state != INITD_SERVICE_FAILED) {
            continue;
        }
        start_result result = request_service_start_locked(entry);
        if (result == start_result::STARTED) {
            printf("init: spawned %s from kick\n", entry->name);
        }
    }
}

void initialize_registry() {
    pthread_mutex_lock(&g_services_lock);
    for (unsigned i = 0; i < k_service_config_count; ++i) {
        const auto& config = k_service_configs[i];
        auto* entry = ensure_service_locked(config.name);
        if (!entry) {
            pthread_mutex_unlock(&g_services_lock);
            printf("init: service registry full while loading config\n");
            return;
        }
        entry->path = config.path;
        entry->autostart = config.autostart;
        entry->restart_on_failure = config.restart_on_failure;
        entry->max_restart_attempts = config.max_restart_attempts;
        entry->restart_delay_ms = config.restart_delay_ms;
        entry->dependency_count = config.dependency_count;
        for (uint32_t dep = 0; dep < config.dependency_count && dep < MAX_SERVICE_DEPS; ++dep) {
            entry->dependencies[dep] = config.dependencies[dep];
        }
        entry->state = INITD_SERVICE_STOPPED;
    }
    pthread_mutex_unlock(&g_services_lock);
}

void start_autostart_services() {
    pthread_mutex_lock(&g_services_lock);
    for (int i = 0; i < g_service_count; ++i) {
        auto* entry = &g_registry[i];
        if (!entry->autostart || !entry->path) continue;

        entry->start_requested = true;
        start_result result = request_service_start_locked(entry);
        if (result == start_result::FAILED) {
            printf("init: failed to spawn %s\n", entry->name);
            continue;
        }
        if (result == start_result::DEFERRED) {
            printf("init: deferred %s until dependencies are ready\n", entry->name);
            continue;
        }
        printf("init: spawned %s as tid %u\n", entry->name, entry->tid);
    }
    pthread_mutex_unlock(&g_services_lock);
}

void* reaper_main(void*) {
    while (true) {
        int status = 0;
        pid_t tid = waitpid(-1, &status, 0);
        if (tid <= 0) {
            usleep(10000); // 10ms wait to avoid busy loop
            continue;
        }

        pthread_mutex_lock(&g_services_lock);
        auto* entry = find_service_by_tid_locked(static_cast<uint32_t>(tid));
        if (entry) {
            entry->tid = 0;
            entry->registered = false;
            entry->last_exit_status = static_cast<uint32_t>(status);

            if (entry->stop_requested) {
                entry->state = INITD_SERVICE_STOPPED;
                entry->restart_budget_used = 0;
                entry->start_requested = false;
            } else if (WIFSIGNALED(status)) {
                entry->state = INITD_SERVICE_FAILED;
                entry->failure_count++;
                schedule_restart_locked(entry);
            } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                entry->state = INITD_SERVICE_STOPPED;
                entry->restart_budget_used = 0;
                entry->start_requested = false;
            } else {
                entry->state = INITD_SERVICE_FAILED;
                entry->failure_count++;
                schedule_restart_locked(entry);
            }

            entry->stop_requested = false;
            printf("init: reaped %s (tid %d, status 0x%x)\n", entry->name, tid, status);
        }
        kick_pending_services_locked();
        pthread_mutex_unlock(&g_services_lock);
    }

    return nullptr;
}

void handle_register(const message& msg) {
    char name[INITD_SERVICE_NAME_MAX] = {};
    decode_name(name, msg);
    pthread_mutex_lock(&g_services_lock);
    auto* entry = ensure_service_locked(name);
    if (!entry) {
        pthread_mutex_unlock(&g_services_lock);
        reply_control(msg.sender, INITD_CTL_EFAIL, 0, INITD_SERVICE_FAILED);
        return;
    }

    if (entry->tid != 0 && entry->tid != msg.sender) {
        uint32_t tid = entry->tid;
        uint32_t state = entry->state;
        uint64_t meta = pack_service_meta(entry);
        pthread_mutex_unlock(&g_services_lock);
        printf("init: ignoring duplicate registration for %s from %u\n", entry->name, msg.sender);
        reply_control(msg.sender, INITD_CTL_EBUSY, tid, state, meta);
        return;
    }

    entry->tid = msg.sender;
    entry->registered = true;
    entry->stop_requested = false;
    entry->restart_pending = false;
    entry->restart_budget_used = 0;
    entry->start_requested = false;
    entry->state = INITD_SERVICE_RUNNING;
    uint32_t tid = entry->tid;
    uint32_t state = entry->state;
    uint64_t meta = pack_service_meta(entry);
    printf("init: registered service %s -> tid %u\n", entry->name, entry->tid);
    kick_pending_services_locked();
    pthread_mutex_unlock(&g_services_lock);
    reply_control(msg.sender, INITD_CTL_OK, tid, state, meta);
}

void handle_lookup(const message& msg) {
    char name[INITD_SERVICE_NAME_MAX] = {};
    decode_name(name, msg);
    pthread_mutex_lock(&g_services_lock);
    auto* entry = find_service_locked(name);
    uint32_t tid = entry ? entry->tid : 0;
    pthread_mutex_unlock(&g_services_lock);
    reply_lookup(msg.sender, tid);
}

void handle_start(const message& msg) {
    char name[INITD_SERVICE_NAME_MAX] = {};
    decode_name(name, msg);
    pthread_mutex_lock(&g_services_lock);
    auto* entry = find_service_locked(name);
    if (!entry || !entry->path) {
        pthread_mutex_unlock(&g_services_lock);
        reply_control(msg.sender, INITD_CTL_ENOENT, 0, INITD_SERVICE_UNKNOWN);
        return;
    }

    if (entry->restart_pending) {
        entry->restart_pending = false;
        entry->restart_generation++;
    }

    if (entry->tid != 0 || entry->state == INITD_SERVICE_STARTING || entry->state == INITD_SERVICE_RUNNING ||
        entry->state == INITD_SERVICE_STOPPING) {
        uint32_t tid = entry->tid;
        uint32_t state = entry->state;
        uint64_t meta = pack_service_meta(entry);
        pthread_mutex_unlock(&g_services_lock);
        reply_control(msg.sender, INITD_CTL_EBUSY, tid, state, meta);
        return;
    }

    entry->start_requested = true;
    start_result result = request_service_start_locked(entry);
    long tid = (result == start_result::STARTED) ? static_cast<long>(entry->tid) : -1;
    uint32_t state = entry->state;
    uint64_t meta = pack_service_meta(entry);
    pthread_mutex_unlock(&g_services_lock);
    if (result == start_result::FAILED) {
        reply_control(msg.sender, INITD_CTL_EFAIL, 0, state, meta);
        return;
    }
    if (result == start_result::DEFERRED) {
        printf("init: waiting to start %s until dependencies are ready\n", name);
        reply_control(msg.sender, INITD_CTL_OK, 0, state, meta);
        return;
    }

    printf("init: started %s as tid %ld\n", name, tid);
    reply_control(msg.sender, INITD_CTL_OK, static_cast<uint32_t>(tid), state, meta);
}

void handle_stop(const message& msg) {
    char name[INITD_SERVICE_NAME_MAX] = {};
    decode_name(name, msg);
    pthread_mutex_lock(&g_services_lock);
    auto* entry = find_service_locked(name);
    if (!entry) {
        pthread_mutex_unlock(&g_services_lock);
        reply_control(msg.sender, INITD_CTL_ENOENT, 0, INITD_SERVICE_UNKNOWN);
        return;
    }

    if (entry->tid == 0) {
        entry->restart_pending = false;
        entry->restart_generation++;
        entry->stop_requested = false;
        entry->start_requested = false;
        entry->restart_budget_used = 0;
        entry->state = INITD_SERVICE_STOPPED;
        uint64_t meta = pack_service_meta(entry);
        pthread_mutex_unlock(&g_services_lock);
        reply_control(msg.sender, INITD_CTL_OK, 0, INITD_SERVICE_STOPPED, meta);
        return;
    }

    if (names_equal(entry->name, "init")) {
        uint32_t tid = entry->tid;
        uint32_t state = entry->state;
        uint64_t meta = pack_service_meta(entry);
        pthread_mutex_unlock(&g_services_lock);
        reply_control(msg.sender, INITD_CTL_EPERM, tid, state, meta);
        return;
    }

    uint32_t tid = entry->tid;
    if (syscall(SYS_KILL, tid, SIGKILL) < 0) {
        uint32_t state = entry->state;
        uint64_t meta = pack_service_meta(entry);
        pthread_mutex_unlock(&g_services_lock);
        reply_control(msg.sender, INITD_CTL_EFAIL, tid, state, meta);
        return;
    }

    entry->stop_requested = true;
    entry->restart_pending = false;
    entry->restart_generation++;
    entry->start_requested = false;
    entry->state = INITD_SERVICE_STOPPING;
    uint32_t state = entry->state;
    uint64_t meta = pack_service_meta(entry);
    pthread_mutex_unlock(&g_services_lock);

    printf("init: stopping %s (tid %u)\n", name, tid);
    reply_control(msg.sender, INITD_CTL_OK, tid, state, meta);
}

void handle_status(const message& msg) {
    char name[INITD_SERVICE_NAME_MAX] = {};
    decode_name(name, msg);
    pthread_mutex_lock(&g_services_lock);
    auto* entry = find_service_locked(name);
    if (!entry) {
        pthread_mutex_unlock(&g_services_lock);
        reply_control(msg.sender, INITD_CTL_ENOENT, 0, INITD_SERVICE_UNKNOWN);
        return;
    }

    uint32_t tid = entry->tid;
    uint32_t state = entry->state;
    uint64_t meta = pack_service_meta(entry);
    pthread_mutex_unlock(&g_services_lock);
    reply_control(msg.sender, INITD_CTL_OK, tid, state, meta);
}

} // namespace

int main() {
    printf("init: service manager online\n");
    initialize_registry();

    pthread_t reaper_thread = 0;
    if (pthread_create(&reaper_thread, nullptr, reaper_main, nullptr) != 0) {
        printf("init: failed to start reaper thread\n");
        return 1;
    }

    start_autostart_services();

    while (true) {
        message msg = {};
        syscall(SYS_IPC_RECV, reinterpret_cast<long>(&msg), 0, 0);

        switch (msg.type) {
        case INITD_MSG_REGISTER_SERVICE:
            handle_register(msg);
            break;
        case INITD_MSG_LOOKUP_SERVICE:
            handle_lookup(msg);
            break;
        case INITD_MSG_START_SERVICE:
            handle_start(msg);
            break;
        case INITD_MSG_STOP_SERVICE:
            handle_stop(msg);
            break;
        case INITD_MSG_STATUS_SERVICE:
            handle_status(msg);
            break;
        default:
            printf("init: unhandled message type %u from %u\n", msg.type, msg.sender);
            break;
        }
    }

    return 0;
}
