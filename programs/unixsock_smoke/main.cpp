// SPDX-License-Identifier: MIT
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

namespace {

constexpr int RETRY_LIMIT = 2000;
constexpr useconds_t RETRY_DELAY_US = 1000;
constexpr const char* SMOKE_MARKER_PATH = "/run/log/unixsock_smoke.ok";
constexpr const char* PASS_MARKER = "PASS\n";

#if !defined(__arm__)
struct accept_ctx {
    int listener_fd;
    int result;
};

unsigned int unix_addr_len(const sockaddr_un& addr, size_t name_len, bool abstract) {
    return static_cast<unsigned int>(sizeof(addr.sun_family) + name_len + (abstract ? 1 : 1));
}

void init_abstract_addr(sockaddr_un& addr, const char* name) {
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    size_t len = strlen(name);
    if (len > sizeof(addr.sun_path) - 1) {
        len = sizeof(addr.sun_path) - 1;
    }
    memcpy(addr.sun_path + 1, name, len);
}
#endif

int wait_recv_eq(int fd, const char* expected) {
    char buffer[64] = {};
    size_t expected_len = strlen(expected);

    for (int attempt = 0; attempt < RETRY_LIMIT; ++attempt) {
        ssize_t n = recv(fd, buffer, sizeof(buffer) - 1, 0);
        if (n < 0) {
            sched_yield();
            usleep(RETRY_DELAY_US);
            continue;
        }
        buffer[n] = '\0';
        if (static_cast<size_t>(n) != expected_len || strcmp(buffer, expected) != 0) {
            printf("unixsock_smoke: recv mismatch on fd %d: got '%s'\n", fd, buffer);
            return -1;
        }
        return 0;
    }

    printf("unixsock_smoke: recv timeout on fd %d\n", fd);
    return -1;
}

int test_socketpair() {
    int sv[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        printf("unixsock_smoke: socketpair() failed\n");
        return 1;
    }

    if (send(sv[0], "pair-ping", 9, 0) != 9) {
        printf("unixsock_smoke: socketpair send() failed\n");
        close(sv[0]);
        close(sv[1]);
        return 1;
    }
    if (wait_recv_eq(sv[1], "pair-ping") < 0) {
        close(sv[0]);
        close(sv[1]);
        return 1;
    }

    if (send(sv[1], "pair-pong", 9, 0) != 9) {
        printf("unixsock_smoke: socketpair reply send() failed\n");
        close(sv[0]);
        close(sv[1]);
        return 1;
    }
    if (wait_recv_eq(sv[0], "pair-pong") < 0) {
        close(sv[0]);
        close(sv[1]);
        return 1;
    }

    close(sv[0]);
    close(sv[1]);
    printf("unixsock_smoke: socketpair path ok\n");
    return 0;
}

#if !defined(__arm__)
void* accept_worker(void* arg) {
    auto* ctx = static_cast<accept_ctx*>(arg);
    ctx->result = -1;

    int accepted_fd = -1;
    for (int attempt = 0; attempt < RETRY_LIMIT; ++attempt) {
        accepted_fd = accept(ctx->listener_fd, nullptr, nullptr);
        if (accepted_fd >= 0) {
            break;
        }
        sched_yield();
        usleep(RETRY_DELAY_US);
    }

    if (accepted_fd < 0) {
        printf("unixsock_smoke: accept() did not complete\n");
        return nullptr;
    }

    if (wait_recv_eq(accepted_fd, "connect-ping") < 0) {
        close(accepted_fd);
        return nullptr;
    }

    if (send(accepted_fd, "connect-pong", 12, 0) != 12) {
        printf("unixsock_smoke: send() failed on accepted socket\n");
        close(accepted_fd);
        return nullptr;
    }

    close(accepted_fd);
    ctx->result = 0;
    return nullptr;
}

int test_connect_accept() {
    int listener_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener_fd < 0) {
        printf("unixsock_smoke: listener socket() failed\n");
        return 1;
    }

    sockaddr_un addr = {};
    init_abstract_addr(addr, "rucux-unixsock-smoke");
    unsigned int addr_len = unix_addr_len(addr, strlen("rucux-unixsock-smoke"), true);

    if (bind(listener_fd, reinterpret_cast<sockaddr*>(&addr), addr_len) < 0) {
        printf("unixsock_smoke: bind() failed\n");
        close(listener_fd);
        return 1;
    }
    if (listen(listener_fd, 1) < 0) {
        printf("unixsock_smoke: listen() failed\n");
        close(listener_fd);
        return 1;
    }

    accept_ctx ctx = {listener_fd, -1};
    pthread_t worker = 0;
    if (pthread_create(&worker, nullptr, accept_worker, &ctx) != 0) {
        printf("unixsock_smoke: pthread_create() failed\n");
        close(listener_fd);
        return 1;
    }

    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd < 0) {
        printf("unixsock_smoke: client socket() failed\n");
        close(listener_fd);
        return 1;
    }

    if (connect(client_fd, reinterpret_cast<sockaddr*>(&addr), addr_len) < 0) {
        printf("unixsock_smoke: connect() failed\n");
        close(client_fd);
        close(listener_fd);
        return 1;
    }

    sockaddr_un peer_addr = {};
    socklen_t peer_len = sizeof(peer_addr);
    if (getpeername(client_fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_len) < 0) {
        printf("unixsock_smoke: getpeername() failed\n");
        close(client_fd);
        close(listener_fd);
        return 1;
    }

    if (send(client_fd, "connect-ping", 12, 0) != 12) {
        printf("unixsock_smoke: client send() failed\n");
        close(client_fd);
        close(listener_fd);
        return 1;
    }
    if (wait_recv_eq(client_fd, "connect-pong") < 0) {
        close(client_fd);
        close(listener_fd);
        return 1;
    }

    pthread_join(worker, nullptr);
    close(client_fd);
    close(listener_fd);

    if (ctx.result != 0) {
        printf("unixsock_smoke: accept/connect worker failed\n");
        return 1;
    }

    printf("unixsock_smoke: connect/accept path ok\n");
    return 0;
}
#endif

bool write_marker(const char* path) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return false;
    }

    bool ok = write(fd, PASS_MARKER, strlen(PASS_MARKER)) == static_cast<ssize_t>(strlen(PASS_MARKER));
    close(fd);
    return ok;
}

} // namespace

int main() {
    printf("unixsock_smoke: starting\n");

    if (test_socketpair() != 0) {
        printf("unixsock_smoke: FAILED at socketpair\n");
        return 1;
    }

#if defined(__arm__)
    printf("unixsock_smoke: connect/accept skipped on armv7\n");
#else
    if (test_connect_accept() != 0) {
        printf("unixsock_smoke: FAILED at connect/accept\n");
        return 1;
    }
#endif

    if (!write_marker(SMOKE_MARKER_PATH)) {
        printf("unixsock_smoke: FAILED at marker write\n");
        return 1;
    }

    printf("unixsock_smoke: PASS\n");
    return 0;
}
