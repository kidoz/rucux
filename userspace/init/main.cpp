// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <uapi/kernel/syscalls.h>

extern "C" {
struct message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};

long syscall(long num, long a1 = 0, long a2 = 0, long a3 = 0) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}
}

struct service_entry {
    char name[16];
    uint32_t tid;
};

static service_entry g_registry[16];
static int g_service_count = 0;

int main() {
    // PID 1 is the Root Server (Init)
    while (true) {
        message m;
        syscall(SYS_IPC_RECV, (long)&m, 0, 0);

        if (m.type == 10) { // REGISTER_SERVICE
            // Simplified: first 16 bytes of data is name
            const char* name = reinterpret_cast<const char*>(m.data);
            for (int i = 0; i < 16 && name[i]; ++i)
                g_registry[g_service_count].name[i] = name[i];
            g_registry[g_service_count].tid = m.sender;
            g_service_count++;

            // Ack
            message ack;
            ack.type = 0;
            syscall(SYS_IPC_SEND, m.sender, (long)&ack);
        } else if (m.type == 11) { // LOOKUP_SERVICE
            const char* name = reinterpret_cast<const char*>(m.data);
            uint32_t found_tid = 0;
            for (int i = 0; i < g_service_count; ++i) {
                bool match = true;
                for (int j = 0; j < 16 && name[j]; ++j) {
                    if (g_registry[i].name[j] != name[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    found_tid = g_registry[i].tid;
                    break;
                }
            }

            message resp;
            resp.type = 11;
            resp.data[0] = found_tid;
            syscall(SYS_IPC_SEND, m.sender, (long)&resp);
        }
    }
    return 0;
}
