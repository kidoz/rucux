// SPDX-License-Identifier: MIT
#pragma once

#include <kernel/net/netbuf.hpp>
#include <kernel/net/netif.hpp>
#include <stdint.h>

namespace arch::armv7::dwmac {

// DesignWare MAC MMIO Base for Amlogic S905 (Odroid C2)
constexpr uintptr_t MAC_BASE = 0xC9410000;

// MAC Registers
constexpr uint32_t MAC_CONFIG = 0x0000;
constexpr uint32_t MAC_FRAME_FILTER = 0x0004;
constexpr uint32_t GMII_ADDRESS = 0x0010;
constexpr uint32_t GMII_DATA = 0x0014;
constexpr uint32_t MAC_ADDR_HIGH0 = 0x0040;
constexpr uint32_t MAC_ADDR_LOW0 = 0x0044;

// DMA Registers
constexpr uint32_t DMA_BUS_MODE = 0x1000;
constexpr uint32_t DMA_TX_POLL_DEMAND = 0x1004;
constexpr uint32_t DMA_RX_POLL_DEMAND = 0x1008;
constexpr uint32_t DMA_RX_BASE_ADDR = 0x100C;
constexpr uint32_t DMA_TX_BASE_ADDR = 0x1010;
constexpr uint32_t DMA_STATUS = 0x1014;
constexpr uint32_t DMA_OPERATION_MODE = 0x1018;
constexpr uint32_t DMA_INTERRUPT_EN = 0x101C;

// DMA Status Bits
constexpr uint32_t DMA_STATUS_RI = (1 << 6);   // Receive Interrupt
constexpr uint32_t DMA_STATUS_TI = (1 << 0);   // Transmit Interrupt
constexpr uint32_t DMA_STATUS_NIS = (1 << 16); // Normal Interrupt Summary

// RTL8211F PHY standard registers
constexpr uint32_t MII_BMCR = 0x00;
constexpr uint32_t MII_BMSR = 0x01;
constexpr uint32_t MII_PHYSID1 = 0x02;
constexpr uint32_t MII_PHYSID2 = 0x03;

// RTL8211F PHY paged registers
constexpr uint32_t RTL8211F_PAGE_SELECT = 0x1F;
constexpr uint32_t RTL8211F_TX_RX_DELAY = 0x19;
constexpr uint32_t RTL8211F_PAGE_EXT = 0xA43;

// DMA Descriptor
struct alignas(16) dma_desc {
    volatile uint32_t des0; // Status / OWN bit
    volatile uint32_t des1; // Control / Buffer length
    volatile uint32_t des2; // Buffer 1 address pointer
    volatile uint32_t des3; // Buffer 2 address pointer / Next descriptor
};

// DMA Descriptor DES0 bits
constexpr uint32_t DESC_OWN = (1U << 31);
constexpr uint32_t DESC_RX_FIRST = (1 << 9);
constexpr uint32_t DESC_RX_LAST = (1 << 8);
constexpr uint32_t DESC_TX_LAST = (1 << 29);
constexpr uint32_t DESC_TX_FIRST = (1 << 28);
constexpr uint32_t DESC_TX_IC = (1 << 30); // Interrupt on completion

// DMA Descriptor DES1 bits
constexpr uint32_t DESC_TX_CHAIN = (1 << 20); // Second address chained
constexpr uint32_t DESC_RX_CHAIN = (1 << 14); // Second address chained

void init() noexcept;

} // namespace arch::armv7::dwmac
