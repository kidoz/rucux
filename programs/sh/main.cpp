// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <uapi/kernel/initd.h>
#include <uapi/kernel/syscalls.h>
#include <uapi/kernel/top.h>
#include <unistd.h>

extern "C" {
struct message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};
}

struct dirent_compat {
    unsigned long d_ino;
    long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
};

static void copy_service_name(message& msg, const char* name) {
    for (int i = 0; i < INITD_SERVICE_NAME_MAX; ++i) {
        reinterpret_cast<char*>(msg.data)[i] = name[i];
        if (name[i] == '\0') break;
    }
}

static const char* service_state_to_str(uint64_t state) {
    switch (state) {
    case INITD_SERVICE_STARTING:
        return "STARTING";
    case INITD_SERVICE_RUNNING:
        return "RUNNING";
    case INITD_SERVICE_STOPPING:
        return "STOPPING";
    case INITD_SERVICE_STOPPED:
        return "STOPPED";
    case INITD_SERVICE_FAILED:
        return "FAILED";
    case INITD_SERVICE_WAITING:
        return "WAITING";
    default:
        return "UNKNOWN";
    }
}

static int control_service(uint32_t type, const char* name) {
    message req = {};
    req.type = type;
    copy_service_name(req, name);
    syscall(SYS_IPC_SEND, 1, (long)&req);

    message resp = {};
    syscall(SYS_IPC_RECV, (long)&resp, 0, 0);
    if (resp.type != INITD_MSG_CONTROL_REPLY) {
        printf("service: unexpected reply type %u\n", resp.type);
        return 1;
    }

    uint64_t result = resp.data[0];
    uint64_t tid = resp.data[1];
    uint64_t state = resp.data[2];
    uint64_t meta = resp.data[3];
    uint32_t failures = static_cast<uint32_t>((meta >> 48) & 0xffffU);
    uint32_t restarts = static_cast<uint32_t>((meta >> 32) & 0xffffU);
    uint32_t last_exit = static_cast<uint32_t>(meta);

    if (result == INITD_CTL_OK) {
        printf("%s: %s", name, service_state_to_str(state));
        if (tid != 0) printf(" (tid %llu)", (unsigned long long)tid);
        if (type == INITD_MSG_STATUS_SERVICE) {
            printf(", failures=%u", failures);
            printf(", restarts=%u", restarts);
            printf(", last_exit=0x%x", last_exit);
        }
        printf("\n");
        return 0;
    }

    const char* err = "failed";
    switch (result) {
    case INITD_CTL_ENOENT:
        err = "unknown service";
        break;
    case INITD_CTL_EBUSY:
        err = "already active";
        break;
    case INITD_CTL_EFAIL:
        err = "operation failed";
        break;
    case INITD_CTL_EPERM:
        err = "operation denied";
        break;
    default:
        break;
    }

    printf("service: %s: %s\n", name, err);
    return 1;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    message reg = {};
    reg.type = INITD_MSG_REGISTER_SERVICE;
    copy_service_name(reg, "sh");
    syscall(SYS_IPC_SEND, 1, (long)&reg);

    message ack = {};
    syscall(SYS_IPC_RECV, (long)&ack, 0, 0);

    printf("\n==================================\n");
    printf("   Welcome to rucux sh (built-in)\n");
    printf("==================================\n\n");

    char buf[256];
    while (true) {
        while (waitpid(-1, nullptr, WNOHANG) > 0) {
        }
        printf("$ ");
        fflush(stdout);

        long n = syscall(SYS_READ, 0, (long)buf, sizeof(buf) - 1);
        if (n == 0) {
            printf("logout\n");
            return 0;
        }
        if (n < 0) {
            printf("sh: terminal read failed\n");
            return 1;
        }

        buf[n] = '\0';

        // trim newline
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n' || buf[i] == '\r') {
                buf[i] = '\0';
                break;
            }
        }

        if (buf[0] == '\0') continue;

        if (strcmp(buf, "exit") == 0) {
            printf("logout\n");
            return 0;
        } else if (strcmp(buf, "uptime") == 0) {
            struct timespec now{};
            if (clock_gettime(CLOCK_MONOTONIC, &now) == 0)
                printf("uptime: %ld.%03ld seconds\n", now.tv_sec, now.tv_nsec / 1000000);
            else
                printf("uptime: clock unavailable\n");
        } else if (strncmp(buf, "start ", 6) == 0) {
            const char* path = buf + 6;
            while (*path == ' ')
                ++path;
            long child = *path == '/' ? syscall(SYS_SPAWN, reinterpret_cast<long>(path)) : -1;
            if (child < 0)
                printf("start: cannot start %s\n", path);
            else
                printf("start: pid %ld\n", child);
        } else if (strncmp(buf, "run ", 4) == 0) {
            const char* path = buf + 4;
            while (*path == ' ')
                ++path;
            long child = syscall(SYS_SPAWN, reinterpret_cast<long>(path));
            if (child < 0)
                printf("run: cannot start %s\n", path);
            else {
                int status = 0;
                if (waitpid(child, &status, 0) < 0)
                    printf("run: wait failed\n");
                else
                    printf("run: exit status %d\n", WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
            }
        } else if (strncmp(buf, "help", 4) == 0) {
            printf("Built-in commands:\n");
            printf("  help     - show this message\n");
            printf("  ls <dir> - list directory contents\n");
            printf("  cat <f>  - print file contents\n");
            printf("  echo <m> - print message\n");
            printf("  top      - show processes\n");
            printf("  uptime   - show elapsed boot time\n");
            printf("  run <absolute-path> - run a program and wait (no arguments yet)\n");
            printf("  start <absolute-path> - start a background program (no arguments)\n");
            printf("  exit     - close the shell\n");
            printf("  service <start|stop|status> <name>\n");
        } else if (strncmp(buf, "ls", 2) == 0) {
            const char* path = buf + 2;
            while (*path == ' ')
                path++;
            if (*path == '\0') path = "/fat32";

            int fd = syscall(SYS_OPEN, (long)path, 0);
            if (fd < 0) {
                printf("ls: cannot open %s\n", path);
            } else {
                dirent_compat d;
                while (syscall(SYS_GETDENTS, fd, (long)&d, sizeof(d)) > 0) {
                    printf("%s  ", d.d_name);
                }
                printf("\n");
                syscall(SYS_CLOSE, fd);
            }
        } else if (strncmp(buf, "cat", 3) == 0) {
            const char* path = buf + 3;
            while (*path == ' ')
                path++;

            if (*path == '\0') {
                printf("cat: missing file operand\n");
                continue;
            }

            int fd = syscall(SYS_OPEN, (long)path, 0);
            if (fd < 0) {
                printf("cat: %s: No such file or directory\n", path);
            } else {
                char filebuf[1024];
                long bytes;
                while ((bytes = syscall(SYS_READ, fd, (long)filebuf, sizeof(filebuf))) > 0) {
                    syscall(SYS_WRITE, 1, (long)filebuf, bytes);
                }
                syscall(SYS_CLOSE, fd);
            }
        } else if (strncmp(buf, "echo", 4) == 0) {
            const char* msg = buf + 4;
            while (*msg == ' ')
                msg++;
            printf("%s\n", msg);
        } else if (strncmp(buf, "top", 3) == 0) {
            struct top_info info;
            int ret = syscall(SYS_TOP, (long)&info, sizeof(info));
            if (ret < 0) {
                printf("top: sys_top failed\n");
            } else {
                printf("\n  TID | STATE    | PRIO | CPU\n");
                printf("--------------------------------\n");
                for (uint32_t i = 0; i < info.num_processes; ++i) {
                    const char* state_str = "UNKNOWN";
                    switch (info.processes[i].state) {
                    case 0:
                        state_str = "READY";
                        break;
                    case 1:
                        state_str = "RUNNING";
                        break;
                    case 2:
                        state_str = "BLOCKED";
                        break;
                    case 3:
                        state_str = "TERMINAT";
                        break;
                    }
                    printf(" %4d | %-8s | %4d | %3d\n", info.processes[i].tid, state_str, info.processes[i].priority,
                           info.processes[i].cpu);
                }
                printf("\nTotal threads: %d\n", info.num_processes);
            }
        } else if (strncmp(buf, "service", 7) == 0) {
            char* args = buf + 7;
            while (*args == ' ')
                args++;

            char* action = args;
            while (*args && *args != ' ')
                args++;
            if (*args == '\0') {
                printf("usage: service <start|stop|status> <name>\n");
                continue;
            }

            *args++ = '\0';
            while (*args == ' ')
                args++;
            if (*args == '\0') {
                printf("usage: service <start|stop|status> <name>\n");
                continue;
            }

            if (strcmp(action, "start") == 0) {
                control_service(INITD_MSG_START_SERVICE, args);
            } else if (strcmp(action, "stop") == 0) {
                control_service(INITD_MSG_STOP_SERVICE, args);
            } else if (strcmp(action, "status") == 0) {
                control_service(INITD_MSG_STATUS_SERVICE, args);
            } else {
                printf("service: unknown action: %s\n", action);
            }
        } else {
            printf("sh: command not found: %s\n", buf);
        }
    }

    return 0;
}
