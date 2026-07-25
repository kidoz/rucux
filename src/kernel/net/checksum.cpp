// SPDX-License-Identifier: MIT
#include <kernel/net/ipv4.hpp>

namespace kernel::net {

uint16_t checksum(const void* data, size_t len) noexcept {
    uint32_t sum = 0;
    auto* p = reinterpret_cast<const uint16_t*>(data);

    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len) sum += *reinterpret_cast<const uint8_t*>(p);

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return static_cast<uint16_t>(~sum);
}

uint16_t checksum_pseudo(uint32_t src, uint32_t dst, uint8_t proto, const void* data, size_t len) noexcept {
    uint32_t sum = 0;

    // Pseudo-header
    sum += (src >> 0) & 0xFFFF;
    sum += (src >> 16) & 0xFFFF;
    sum += (dst >> 0) & 0xFFFF;
    sum += (dst >> 16) & 0xFFFF;
    sum += htons(proto);
    sum += htons(static_cast<uint16_t>(len));

    // Data
    auto* p = reinterpret_cast<const uint16_t*>(data);
    size_t remaining = len;
    while (remaining > 1) {
        sum += *p++;
        remaining -= 2;
    }
    if (remaining) sum += *reinterpret_cast<const uint8_t*>(p);

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return static_cast<uint16_t>(~sum);
}

} // namespace kernel::net
