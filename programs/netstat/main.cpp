// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <uapi/kernel/net.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>
static void ip(uint32_t value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    printf("%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
}
int main() {
    rucux_net_info info{};
    if (syscall(SYS_NET_INFO, reinterpret_cast<long>(&info)) < 0) {
        printf("netstat: unavailable\n");
        return 1;
    }
    for (uint32_t i = 0; i < info.count && i < RUCUX_NETIF_MAX; ++i) {
        const auto& iface = info.interfaces[i];
        printf("%s: address=", iface.name);
        ip(iface.address);
        printf(" mask=");
        ip(iface.netmask);
        printf(" gateway=");
        ip(iface.gateway);
        printf(" mtu=%u\n", iface.mtu);
    }
    return 0;
}
