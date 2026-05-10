// SPDX-License-Identifier: MIT
#include <arch/armv7/dwmac.hpp>
#include <kernel/print.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/sync/spinlock.hpp>
#include <lib/string.hpp>

namespace arch::armv7::dwmac {

// Constants
constexpr uint32_t NUM_RX_DESC = 32;
constexpr uint32_t NUM_TX_DESC = 32;

// Memory mapped I/O
static inline uint32_t read_reg(uint32_t offset) noexcept {
    return *reinterpret_cast<volatile uint32_t*>(MAC_BASE + offset);
}

static inline void write_reg(uint32_t offset, uint32_t value) noexcept {
    *reinterpret_cast<volatile uint32_t*>(MAC_BASE + offset) = value;
}

// MDIO Bus
static uint16_t mdio_read(uint8_t phy_addr, uint8_t reg) noexcept {
    // Wait until MDIO is not busy (bit 0 is GW_BUSY)
    while (read_reg(GMII_ADDRESS) & 1) {}

    // Construct GMII Address
    // PA (PHY Addr) = bits 15:11, GR (Reg Addr) = bits 10:6
    // CR (Clock Range) = bits 5:2 (Let's use 0b0100 for 100-150MHz)
    // GB (Busy) = bit 0, GW (Write) = bit 1 (0 for read)
    uint32_t addr_val = (phy_addr << 11) | (reg << 6) | (4 << 2) | 1;
    write_reg(GMII_ADDRESS, addr_val);

    while (read_reg(GMII_ADDRESS) & 1) {}

    return static_cast<uint16_t>(read_reg(GMII_DATA) & 0xFFFF);
}

static void mdio_write(uint8_t phy_addr, uint8_t reg, uint16_t data) noexcept {
    while (read_reg(GMII_ADDRESS) & 1) {}

    write_reg(GMII_DATA, data);

    // GW (Write) = bit 1 (1 for write)
    uint32_t addr_val = (phy_addr << 11) | (reg << 6) | (4 << 2) | 3;
    write_reg(GMII_ADDRESS, addr_val);

    while (read_reg(GMII_ADDRESS) & 1) {}
}

// DMA state
static dma_desc* rx_ring = nullptr;
static dma_desc* tx_ring = nullptr;
static kernel::net::netbuf* rx_bufs[NUM_RX_DESC] = {};
static kernel::net::netbuf* tx_bufs[NUM_TX_DESC] = {};

static uint32_t tx_cur = 0;

static kernel::net::netif g_dwmac_iface;
static kernel::irq_spinlock g_tx_lock;

static void dwmac_transmit(kernel::net::netif* iface, kernel::net::netbuf* buf) noexcept {
    (void)iface;
    kernel::irq_lock_guard guard(g_tx_lock);

    dma_desc* desc = &tx_ring[tx_cur];

    if (desc->des0 & DESC_OWN) {
        kernel::print("dwmac: TX ring full!\n");
        // We drop the packet if full (or queue it depending on design)
        kernel::net::netbuf::free(buf);
        return;
    }

    // Copy data to a physical bounce buffer (or map it)
    // For simplicity without an IOMMU, we'll allocate a PMM page if needed,
    // or assume buf->data is physically contiguous.
    // The netbuf should be physically mapped linearly in this kernel map.
    uint32_t phys_addr = reinterpret_cast<uint32_t>(buf->data()); // Assuming 1:1 map for now

    desc->des2 = phys_addr;
    desc->des1 = (buf->len() & 0x1FFF) | DESC_TX_CHAIN;

    // First and last segment + interrupt on completion
    desc->des0 = DESC_OWN | DESC_TX_FIRST | DESC_TX_LAST | DESC_TX_IC;

    tx_bufs[tx_cur] = buf;
    tx_cur = (tx_cur + 1) % NUM_TX_DESC;

    // Ensure memory is written before we trigger MAC
    asm volatile("dmb sy" ::: "memory");

    // Demand transmit
    write_reg(DMA_TX_POLL_DEMAND, 1);
}

static void rtl8211f_init(uint8_t phy_addr) noexcept {
    uint16_t id1 = mdio_read(phy_addr, MII_PHYSID1);
    uint16_t id2 = mdio_read(phy_addr, MII_PHYSID2);
    
    if (id1 == 0xFFFF) {
        kernel::print("dwmac: No PHY found at address {}\n", phy_addr);
        return;
    }
    
    kernel::print("dwmac: RTL8211F PHY found! OUI: 0x{:04x}{:04x}\n", id1, id2);

    // RTL8211F Specific: Enable RGMII TX/RX Delay
    mdio_write(phy_addr, RTL8211F_PAGE_SELECT, RTL8211F_PAGE_EXT);
    uint16_t phycr2 = mdio_read(phy_addr, RTL8211F_TX_RX_DELAY);
    
    phycr2 |= (1 << 8); // TX Delay
    phycr2 |= (1 << 3); // RX Delay
    
    mdio_write(phy_addr, RTL8211F_TX_RX_DELAY, phycr2);
    
    // Restore page 0
    mdio_write(phy_addr, RTL8211F_PAGE_SELECT, 0);
    kernel::print("dwmac: RTL8211F RGMII Delays enabled.\n");

    // Auto-negotiate
    uint16_t bmcr = mdio_read(phy_addr, MII_BMCR);
    bmcr |= (1 << 12); // Enable Auto-Neg
    bmcr |= (1 << 9);  // Restart Auto-Neg
    mdio_write(phy_addr, MII_BMCR, bmcr);

    kernel::print("dwmac: Waiting for PHY link...\n");
    for (int i = 0; i < 50; ++i) {
        uint16_t bmsr = mdio_read(phy_addr, MII_BMSR);
        if (bmsr & (1 << 5)) {
            kernel::print("dwmac: Link is UP!\n");
            break;
        }
        // Delay (approximate)
        for (int j = 0; j < 100000; j = j + 1) {
            asm volatile("" ::: "memory");
        }
    }
}

void init() noexcept {
    kernel::print("dwmac: Initializing DesignWare MAC...\n");

    // Reset DMA
    write_reg(DMA_BUS_MODE, 1);
    while (read_reg(DMA_BUS_MODE) & 1) {}

    // Init PHY (Assume address 0 for now)
    rtl8211f_init(0);

    // Allocate Descriptor Rings (needs to be physically contiguous)
    // For now, we use kmalloc which is identity mapped.
    // 32 desc * 16 bytes = 512 bytes (fits in 1 page)
    rx_ring = static_cast<dma_desc*>(kernel::memory::pmm::alloc_pages(1));
    tx_ring = static_cast<dma_desc*>(kernel::memory::pmm::alloc_pages(1));

    if (!rx_ring || !tx_ring) {
        kernel::print("dwmac: Failed to allocate descriptor rings!\n");
        return;
    }

    lib::memset(rx_ring, 0, 4096);
    lib::memset(tx_ring, 0, 4096);

    // Initialize RX Ring
    for (uint32_t i = 0; i < NUM_RX_DESC; ++i) {
        rx_bufs[i] = kernel::net::netbuf::alloc();
        rx_ring[i].des2 = reinterpret_cast<uint32_t>(rx_bufs[i]->buf_start());
        // Chain to next descriptor
        rx_ring[i].des3 = reinterpret_cast<uint32_t>(&rx_ring[(i + 1) % NUM_RX_DESC]);
        // Buffer size (e.g. 2048) and CHAIN flag
        rx_ring[i].des1 = DESC_RX_CHAIN | (2048 & 0x1FFF);
        // Give ownership to MAC
        rx_ring[i].des0 = DESC_OWN;
    }

    // Initialize TX Ring
    for (uint32_t i = 0; i < NUM_TX_DESC; ++i) {
        tx_ring[i].des3 = reinterpret_cast<uint32_t>(&tx_ring[(i + 1) % NUM_TX_DESC]);
        tx_ring[i].des0 = 0; // CPU owns it initially
    }

    // Configure DMA Base Addresses
    write_reg(DMA_RX_BASE_ADDR, reinterpret_cast<uint32_t>(rx_ring));
    write_reg(DMA_TX_BASE_ADDR, reinterpret_cast<uint32_t>(tx_ring));

    // Configure MAC (1Gbps, Full Duplex)
    // Bit 11: Port Select (0 = GMII/MII)
    // Bit 14: Speed 100 (0 = 10 or 1000)
    // Bit 11: Duplex (1 = Full)
    write_reg(MAC_CONFIG, (1 << 11)); 
    
    // Enable Promiscuous mode for testing
    write_reg(MAC_FRAME_FILTER, 1);

    // Start MAC & DMA
    write_reg(MAC_CONFIG, read_reg(MAC_CONFIG) | (1 << 3) | (1 << 2)); // TX/RX Enable
    write_reg(DMA_OPERATION_MODE, read_reg(DMA_OPERATION_MODE) | (1 << 13) | (1 << 1)); // Start TX/RX

    // Setup network interface
    lib::memset(&g_dwmac_iface, 0, sizeof(g_dwmac_iface));
    // Hardcode a MAC address for testing
    g_dwmac_iface.mac.bytes[0] = 0x02;
    g_dwmac_iface.mac.bytes[1] = 0x00;
    g_dwmac_iface.mac.bytes[2] = 0x11;
    g_dwmac_iface.mac.bytes[3] = 0x22;
    g_dwmac_iface.mac.bytes[4] = 0x33;
    g_dwmac_iface.mac.bytes[5] = 0x44;
    
    g_dwmac_iface.transmit = dwmac_transmit;

    // Hardcoded IP for testing: 10.0.2.15
    g_dwmac_iface.ip.addr = (10 << 24) | (0 << 16) | (2 << 8) | 15;
    g_dwmac_iface.ip.netmask = 0x00FFFFFF;

    kernel::net::netif_register(&g_dwmac_iface);

    kernel::print("dwmac: Ethernet Driver Initialized (MAC 02:00:11:22:33:44)\n");
}

} // namespace arch::armv7::dwmac
