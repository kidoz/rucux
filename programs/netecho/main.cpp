// SPDX-License-Identifier: MIT
#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
bool echo_client(int client) {
    while (true) {
        char data[1024];
        pollfd input{client, POLLIN, 0};
        if (poll(&input, 1, 30000) <= 0) return false;
        // Exercise VFS read/write as well as the socket syscall path.
        auto n = read(client, data, sizeof(data));
        if (n < 0) return false;
        if (!n) return true;
        long sent = 0;
        while (sent < n) {
            pollfd output{client, POLLOUT, 0};
            if (poll(&output, 1, 30000) <= 0) return false;
            auto count = write(client, data + sent, n - sent);
            if (count <= 0) return false;
            sent += count;
        }
    }
}
} // namespace

// A finite diagnostic service, suitable for isolated packet/lifecycle tests.
// TCP clients send one stream, half-close it, read the echo, then reconnect.
int main() {
    int udp = socket(AF_INET, SOCK_DGRAM, 0), listener = socket(AF_INET, SOCK_STREAM, 0);
    if (udp < 0 || listener < 0) return 1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19091);
    if (bind(udp, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) return 2;
    addr.sin_port = htons(19092);
    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 || listen(listener, 4) < 0) return 3;
    printf("netecho: ready UDP=19091 TCP=19092\n");
    unsigned connections = 0, datagrams = 0;
    bool done = false;
    while (!done) {
        pollfd ready[] = {{udp, POLLIN, 0}, {listener, POLLIN, 0}};
        if (poll(ready, 2, 30000) <= 0) break;
        if (ready[0].revents & POLLIN) {
            char data[1472];
            sockaddr_in peer{};
            socklen_t peer_size = sizeof(peer);
            auto n = recvfrom(udp, data, sizeof(data), 0, reinterpret_cast<sockaddr*>(&peer), &peer_size);
            if (n < 0 || sendto(udp, data, n, 0, reinterpret_cast<sockaddr*>(&peer), peer_size) != n) return 4;
            ++datagrams;
            done = n == 4 && memcmp(data, "quit", 4) == 0;
        }
        if (ready[1].revents & POLLIN) {
            int client = accept(listener, nullptr, nullptr);
            if (client < 0) return 5;
            bool ok = echo_client(client);
            close(client);
            ++connections;
            if (!ok) printf("netecho: client failed; continuing\n");
        }
    }
    close(listener);
    close(udp);
    printf("netecho: stopped TCP=%u UDP=%u\n", connections, datagrams);
    return done ? 0 : 10;
}
