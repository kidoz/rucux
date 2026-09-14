// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#define RUCUX_NETIF_MAX 8
struct rucux_netif_info {
    char name[16];
    uint32_t address; // IPv4 fields in network byte order
    uint32_t netmask;
    uint32_t gateway;
    uint32_t mtu;
    uint8_t mac[6];
    uint8_t reserved[2];
};
struct rucux_net_info {
    uint32_t count;
    struct rucux_netif_info interfaces[RUCUX_NETIF_MAX];
};
