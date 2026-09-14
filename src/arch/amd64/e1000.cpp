// SPDX-License-Identifier: MIT
#include <arch/amd64/e1000.hpp>
#include <arch/amd64/idt.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/net/netbuf.hpp>
#include <kernel/net/netif.hpp>
#include <kernel/pci.hpp>
#include <kernel/print.hpp>
#include <lib/string.hpp>

namespace arch::amd64 {

// E1000 Register offsets
namespace reg {
constexpr uint16_t CTRL = 0x0000;
constexpr uint16_t STATUS = 0x0008;
constexpr uint16_t EECD = 0x0010;
constexpr uint16_t EERD = 0x0014;
constexpr uint16_t ICR = 0x00C0;   // Interrupt Cause Read
constexpr uint16_t IMS = 0x00D0;   // Interrupt Mask Set
constexpr uint16_t IMC = 0x00D8;   // Interrupt Mask Clear
constexpr uint16_t RCTL = 0x0100;  // Receive Control
constexpr uint16_t TCTL = 0x0400;  // Transmit Control
constexpr uint16_t RDBAL = 0x2800; // RX Descriptor Base Low
constexpr uint16_t RDBAH = 0x2804; // RX Descriptor Base High
constexpr uint16_t RDLEN = 0x2808; // RX Descriptor Length
constexpr uint16_t RDH = 0x2810;   // RX Descriptor Head
constexpr uint16_t RDT = 0x2818;   // RX Descriptor Tail
constexpr uint16_t TDBAL = 0x3800; // TX Descriptor Base Low
constexpr uint16_t TDBAH = 0x3804; // TX Descriptor Base High
constexpr uint16_t TDLEN = 0x3808; // TX Descriptor Length
constexpr uint16_t TDH = 0x3810;   // TX Descriptor Head
constexpr uint16_t TDT = 0x3818;   // TX Descriptor Tail
constexpr uint16_t RAL0 = 0x5400;  // Receive Address Low
constexpr uint16_t RAH0 = 0x5404;  // Receive Address High
constexpr uint16_t MTA = 0x5200;   // Multicast Table Array
} // namespace reg

// RCTL bits
constexpr uint32_t RCTL_EN = (1 << 1);
constexpr uint32_t RCTL_BAM = (1 << 15);   // Broadcast Accept
constexpr uint32_t RCTL_BSIZE = (0 << 16); // 2048 byte buffers
constexpr uint32_t RCTL_SECRC = (1 << 26); // Strip Ethernet CRC

// TCTL bits
constexpr uint32_t TCTL_EN = (1 << 1);
constexpr uint32_t TCTL_PSP = (1 << 3);

// Descriptor structures (must be 16-byte aligned)
struct rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

constexpr uint8_t RX_DESC_STATUS_DD = (1 << 0); // Descriptor Done
constexpr uint8_t TX_CMD_EOP = (1 << 0);        // End of Packet
constexpr uint8_t TX_CMD_IFCS = (1 << 1);       // Insert FCS
constexpr uint8_t TX_CMD_RS = (1 << 3);         // Report Status
constexpr uint8_t TX_DESC_STATUS_DD = (1 << 0);

// Ring sizes
constexpr int NUM_RX_DESC = 32;
constexpr int NUM_TX_DESC = 32;

static uintptr_t g_mmio_base = 0;
static volatile rx_desc* g_rx_descs = nullptr;
static volatile tx_desc* g_tx_descs = nullptr;
static uint8_t* g_rx_bufs[NUM_RX_DESC] = {};
static uint8_t* g_tx_bufs[NUM_TX_DESC] = {};
static kernel::irq_spinlock g_tx_lock;
static int g_rx_cur = 0;
static int g_tx_cur = 0;
static kernel::net::netif g_e1000_iface;

void e1000::write_reg(uint16_t r, uint32_t value) noexcept {
    *reinterpret_cast<volatile uint32_t*>(g_mmio_base + r) = value;
}

uint32_t e1000::read_reg(uint16_t r) noexcept {
    return *reinterpret_cast<volatile uint32_t*>(g_mmio_base + r);
}

// Read MAC from EEPROM
static bool read_mac(uint8_t mac[6]) noexcept {
    for (int i = 0; i < 3; ++i) {
        e1000::write_reg(reg::EERD, (1) | (static_cast<uint32_t>(i) << 8));
        uint32_t val;
        unsigned tries = 100000;
        do {
            val = e1000::read_reg(reg::EERD);
        } while (!(val & (1 << 4)) && --tries);
        if (!tries) return false;
        mac[i * 2] = val >> 16;
        mac[i * 2 + 1] = val >> 24;
    }
    return true;
}

bool e1000::rx_init() noexcept {
    auto* page = reinterpret_cast<uint8_t*>(kernel::memory::pmm::alloc_page());
    if (!page) return false;
    lib::memset(page, 0, 4096);
    g_rx_descs = reinterpret_cast<volatile rx_desc*>(page);
    for (int i = 0; i < NUM_RX_DESC; ++i) {
        // Dedicated physical pages satisfy the NIC's 2048-byte DMA contract.
        g_rx_bufs[i] = reinterpret_cast<uint8_t*>(kernel::memory::pmm::alloc_page());
        if (!g_rx_bufs[i]) return false;
        g_rx_descs[i].addr = reinterpret_cast<uintptr_t>(g_rx_bufs[i]);
    }
    write_reg(reg::RDBAL, reinterpret_cast<uintptr_t>(page));
    write_reg(reg::RDBAH, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(page)) >> 32);
    write_reg(reg::RDLEN, NUM_RX_DESC * sizeof(rx_desc));
    write_reg(reg::RDH, 0);
    write_reg(reg::RDT, NUM_RX_DESC - 1);
    return true;
}

bool e1000::tx_init() noexcept {
    auto* page = reinterpret_cast<uint8_t*>(kernel::memory::pmm::alloc_page());
    if (!page) return false;
    lib::memset(page, 0, 4096);
    g_tx_descs = reinterpret_cast<volatile tx_desc*>(page);
    for (int i = 0; i < NUM_TX_DESC; ++i) {
        g_tx_bufs[i] = reinterpret_cast<uint8_t*>(kernel::memory::pmm::alloc_page());
        if (!g_tx_bufs[i]) return false;
        g_tx_descs[i].addr = reinterpret_cast<uintptr_t>(g_tx_bufs[i]);
        g_tx_descs[i].status = TX_DESC_STATUS_DD;
    }
    write_reg(reg::TDBAL, reinterpret_cast<uintptr_t>(page));
    write_reg(reg::TDBAH, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(page)) >> 32);
    write_reg(reg::TDLEN, NUM_TX_DESC * sizeof(tx_desc));
    write_reg(reg::TDH, 0);
    write_reg(reg::TDT, 0);
    write_reg(reg::TCTL, TCTL_EN | TCTL_PSP | (0x10 << 4) | (0x40 << 12));
    write_reg(0x0410, 10 | (8 << 10) | (6 << 20)); // Inter-packet gap for copper.
    return true;
}

static void e1000_transmit(kernel::net::netif*, kernel::net::netbuf* buf) noexcept {
    kernel::irq_lock_guard guard(g_tx_lock);
    int idx = g_tx_cur;
    if (buf->len() <= 1514 && (g_tx_descs[idx].status & TX_DESC_STATUS_DD)) {
        lib::memcpy(g_tx_bufs[idx], buf->data(), buf->len());
        g_tx_descs[idx].length = static_cast<uint16_t>(buf->len());
        g_tx_descs[idx].cmd = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
        g_tx_descs[idx].status = 0;
        // x86 coherent DMA: publish data and descriptor before the MMIO doorbell.
        asm volatile("sfence" ::: "memory");
        g_tx_cur = (g_tx_cur + 1) % NUM_TX_DESC;
        e1000::write_reg(reg::TDT, g_tx_cur);
    }
    // Dedicated TX pages remain owned by the driver until descriptor completion.
    // A full ring drops the packet without spinning or freeing in-flight DMA.
    kernel::net::netbuf::free(buf);
}

void e1000::handle_rx() noexcept {
    for (int budget = 0; budget < NUM_RX_DESC; ++budget) {
        auto& descriptor = g_rx_descs[g_rx_cur];
        if (!(descriptor.status & RX_DESC_STATUS_DD)) break;
        asm volatile("lfence" ::: "memory"); // Consume DMA completion before payload.
        uint16_t len = descriptor.length;
        if ((descriptor.status & 2) && !descriptor.errors && len >= 14 && len <= 1514) {
            auto* buf = kernel::net::netbuf::alloc();
            if (buf) {
                lib::memcpy(buf->put(len), g_rx_bufs[g_rx_cur], len);
                kernel::net::netif_input(&g_e1000_iface, buf);
            }
        }
        descriptor.status = 0;
        asm volatile("sfence" ::: "memory");
        int old_cur = g_rx_cur;
        g_rx_cur = (g_rx_cur + 1) % NUM_RX_DESC;
        write_reg(reg::RDT, old_cur);
    }
}

void e1000::irq_handler() noexcept {
    uint32_t icr = read_reg(reg::ICR);
    if (icr & 0x80) handle_rx(); // RX packet
    if (icr & 0x04) link_up();   // Link status change
}

void e1000::link_up() noexcept {
    uint32_t status = read_reg(reg::STATUS);
    kernel::print("E1000: link {}\n", (status & 2) ? "up" : "down");
}

bool e1000::init() noexcept {
    kernel::pci::pci_device dev;
    if (!kernel::pci::find_device(0x8086, 0x100E, dev)) {
        kernel::print("E1000: PCI device not found\n");
        return false;
    }

    if ((dev.bar0 & 7) || !dev.bar0 || dev.interrupt_line >= 16 || dev.interrupt_line < 3 || dev.interrupt_line == 4)
        return false;
    auto command = kernel::pci::read_config_32(dev.bus, dev.device, dev.function, 4) & 0xFFFF;
    kernel::pci::write_config_32(dev.bus, dev.device, dev.function, 4, (command | 6) & ~(1U << 10));

    // Map MMIO region
    g_mmio_base = dev.bar0 & ~0xFUL;
    size_t mmio_size = 128 * 1024; // 128KB MMIO region
    for (size_t i = 0; i < mmio_size; i += 4096) {
        kernel::memory::vmm::map(g_mmio_base + i, g_mmio_base + i,
                                 kernel::memory::page_flags::PRESENT | kernel::memory::page_flags::WRITABLE |
                                     static_cast<kernel::memory::page_flags>(0x18)); // x86 PCD/PWT device mapping
    }

    // Reset
    write_reg(reg::CTRL, read_reg(reg::CTRL) | (1 << 26));
    for (int i = 0; i < 100000; i++)
        asm volatile("" ::: "memory");
    write_reg(reg::CTRL, read_reg(reg::CTRL) & ~(1 << 26));

    // Read MAC address
    write_reg(reg::IMC, 0xFFFFFFFF);
    if (!read_mac(g_e1000_iface.mac.bytes)) return false;
    kernel::print("E1000: MAC {02x}:{02x}:{02x}:{02x}:{02x}:{02x}\n", g_e1000_iface.mac.bytes[0],
                  g_e1000_iface.mac.bytes[1], g_e1000_iface.mac.bytes[2], g_e1000_iface.mac.bytes[3],
                  g_e1000_iface.mac.bytes[4], g_e1000_iface.mac.bytes[5]);

    // Clear multicast table
    for (int i = 0; i < 128; i++)
        write_reg(reg::MTA + i * 4, 0);

    if (!rx_init() || !tx_init()) {
        // Rings have not been enabled; release all partial boot allocations.
        for (auto* page : g_rx_bufs)
            if (page) kernel::memory::pmm::free_page(page);
        for (auto* page : g_tx_bufs)
            if (page) kernel::memory::pmm::free_page(page);
        if (g_rx_descs) kernel::memory::pmm::free_page(const_cast<rx_desc*>(g_rx_descs));
        if (g_tx_descs) kernel::memory::pmm::free_page(const_cast<tx_desc*>(g_tx_descs));
        return false;
    }

    // Register as network interface
    g_e1000_iface.name = "eth0";
    g_e1000_iface.mtu = 1500;
    g_e1000_iface.transmit = e1000_transmit;
    // Default IP config (can be changed by userspace DHCP later)
    g_e1000_iface.ip.addr = 0x0A00000A;    // 10.0.0.10
    g_e1000_iface.ip.netmask = 0x00FFFFFF; // 255.255.255.0
    g_e1000_iface.ip.gateway = 0x0100000A; // 10.0.0.1
    kernel::net::netif_register(&g_e1000_iface);

    // Program the receive address and enable reception only after IRQ setup.
    auto* mac = g_e1000_iface.mac.bytes;
    write_reg(reg::RAL0, mac[0] | (mac[1] << 8) | (mac[2] << 16) | (static_cast<uint32_t>(mac[3]) << 24));
    write_reg(reg::RAH0, mac[4] | (mac[5] << 8) | (1U << 31));
    idt_register_nic_irq(dev.interrupt_line);
    write_reg(reg::CTRL, read_reg(reg::CTRL) | (1 << 6)); // Set Link Up
    write_reg(reg::RCTL, RCTL_EN | RCTL_BAM | RCTL_BSIZE | RCTL_SECRC);
    read_reg(reg::ICR);
    write_reg(reg::IMS, 0x84);
    kernel::print("E1000: initialized on PCI {}:{} IRQ={}\n", dev.bus, dev.device, dev.interrupt_line);
    return true;
}

} // namespace arch::amd64
