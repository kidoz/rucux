// SPDX-License-Identifier: MIT
#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
int main() {
    int receiver = socket(AF_INET, SOCK_DGRAM, 0);
    int sender = socket(AF_INET, SOCK_DGRAM, 0);
    if (receiver < 0 || sender < 0) {
        if (receiver >= 0) close(receiver);
        if (sender >= 0) close(sender);
        return 1;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(19090);
    address.sin_addr.s_addr = htonl(0x7f000001);
    const char payload[] = "rucux-loopback";
    char received[sizeof(payload)]{};
    pollfd ready{receiver, POLLIN, 0};
    bool ok = bind(receiver, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
              sendto(sender, payload, sizeof(payload), 0, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
                  sizeof(payload) &&
              poll(&ready, 1, 1000) == 1 && (ready.revents & POLLIN) &&
              recv(receiver, received, sizeof(received), 0) == sizeof(payload) &&
              memcmp(payload, received, sizeof(payload)) == 0;
    close(sender);
    close(receiver);
    printf("netcheck: UDP loopback %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
