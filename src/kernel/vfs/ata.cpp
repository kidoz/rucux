// SPDX-License-Identifier: MIT
#include <arch/amd64/io.hpp>
#include <kernel/print.hpp>
#include <kernel/vfs/ata.hpp>

// ATA PIO Ports (Primary Bus)
#define ATA_PORT_DATA 0x1F0
#define ATA_PORT_ERROR 0x1F1
#define ATA_PORT_FEATURES 0x1F1
#define ATA_PORT_SECTOR_CNT 0x1F2
#define ATA_PORT_LBA_LO 0x1F3
#define ATA_PORT_LBA_MID 0x1F4
#define ATA_PORT_LBA_HI 0x1F5
#define ATA_PORT_DRV_HEAD 0x1F6
#define ATA_PORT_STATUS 0x1F7
#define ATA_PORT_COMMAND 0x1F7

// Status Register Bits
#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_BSY 0x80

namespace kernel::vfs::ata {

static void wait_bsy() noexcept {
    while (arch::amd64::inb(ATA_PORT_STATUS) & ATA_SR_BSY) {
        // Wait
    }
}

static void wait_drq() noexcept {
    while (!(arch::amd64::inb(ATA_PORT_STATUS) & ATA_SR_DRQ)) {
        if (arch::amd64::inb(ATA_PORT_STATUS) & ATA_SR_ERR) return;
    }
}

bool init() noexcept {
    // Select Primary Master (Drive 0)
    arch::amd64::outb(ATA_PORT_DRV_HEAD, 0xA0);

    // Set sectorcount to 0 and LBA to 0
    arch::amd64::outb(ATA_PORT_SECTOR_CNT, 0);
    arch::amd64::outb(ATA_PORT_LBA_LO, 0);
    arch::amd64::outb(ATA_PORT_LBA_MID, 0);
    arch::amd64::outb(ATA_PORT_LBA_HI, 0);

    // Send IDENTIFY command
    arch::amd64::outb(ATA_PORT_COMMAND, 0xEC);

    // Read status
    uint8_t status = arch::amd64::inb(ATA_PORT_STATUS);
    if (status == 0) {
        kernel::print("ATA: Drive does not exist.\n");
        return false;
    }

    wait_bsy();

    if (arch::amd64::inb(ATA_PORT_LBA_MID) != 0 || arch::amd64::inb(ATA_PORT_LBA_HI) != 0) {
        kernel::print("ATA: Not a standard ATA drive.\n");
        return false;
    }

    wait_drq();

    // Read 256 16-bit words of identification data
    uint16_t id_data[256];
    for (int i = 0; i < 256; i++) {
        id_data[i] = arch::amd64::inw(ATA_PORT_DATA);
    }

    uint32_t lba_sectors = *reinterpret_cast<uint32_t*>(&id_data[60]);
    kernel::print("ATA: Primary Master initialized. Capacity: {} MB\n", (lba_sectors * 512) / (1024 * 1024));

    return true;
}

bool read_sectors(uint32_t lba, uint8_t sector_count, void* buffer) noexcept {
    wait_bsy();

    // Select Drive 0 and send top 4 bits of LBA
    arch::amd64::outb(ATA_PORT_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));

    arch::amd64::outb(ATA_PORT_FEATURES, 0x00);
    arch::amd64::outb(ATA_PORT_SECTOR_CNT, sector_count);
    arch::amd64::outb(ATA_PORT_LBA_LO, (uint8_t)lba);
    arch::amd64::outb(ATA_PORT_LBA_MID, (uint8_t)(lba >> 8));
    arch::amd64::outb(ATA_PORT_LBA_HI, (uint8_t)(lba >> 16));

    // Send READ SECTORS command
    arch::amd64::outb(ATA_PORT_COMMAND, 0x20);

    uint8_t* ptr = static_cast<uint8_t*>(buffer);

    for (int i = 0; i < sector_count; i++) {
        wait_bsy();
        wait_drq();

        if (arch::amd64::inb(ATA_PORT_STATUS) & ATA_SR_ERR) {
            return false;
        }

        // Read 256 words (512 bytes) from DATA port into buffer
        arch::amd64::inw_rep(ATA_PORT_DATA, ptr, 256);
        ptr += 512;
    }

    return true;
}

bool write_sectors(uint32_t lba, uint8_t sector_count, const void* buffer) noexcept {
    wait_bsy();

    // Select Drive 0 and send top 4 bits of LBA
    arch::amd64::outb(ATA_PORT_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));

    arch::amd64::outb(ATA_PORT_FEATURES, 0x00);
    arch::amd64::outb(ATA_PORT_SECTOR_CNT, sector_count);
    arch::amd64::outb(ATA_PORT_LBA_LO, (uint8_t)lba);
    arch::amd64::outb(ATA_PORT_LBA_MID, (uint8_t)(lba >> 8));
    arch::amd64::outb(ATA_PORT_LBA_HI, (uint8_t)(lba >> 16));

    // Send WRITE SECTORS command
    arch::amd64::outb(ATA_PORT_COMMAND, 0x30);

    const uint8_t* ptr = static_cast<const uint8_t*>(buffer);

    for (int i = 0; i < sector_count; i++) {
        wait_bsy();
        wait_drq();

        if (arch::amd64::inb(ATA_PORT_STATUS) & ATA_SR_ERR) {
            return false;
        }

        // Write 256 words (512 bytes) from buffer to DATA port
        arch::amd64::outw_rep(ATA_PORT_DATA, ptr, 256);
        ptr += 512;
    }

    // Cache flush (optional but recommended)
    arch::amd64::outb(ATA_PORT_COMMAND, 0xE7);
    wait_bsy();

    return true;
}

} // namespace kernel::vfs::ata
