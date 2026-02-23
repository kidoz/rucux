// SPDX-License-Identifier: MIT
#include "sd.hpp"

// We need a tiny sleep function since we don't have an OS scheduler or a calibrated timer yet.
// On Cortex-A53, a busy-wait loop roughly translates to cycles.
static void delay_ms(uint32_t ms) {
    // Assuming ~1.5GHz clock, 1ms is ~1.5M cycles.
    // This is an extremely rough approximation for bare-metal bootloader timing.
    for (volatile uint32_t i = 0; i < ms * 10000; i++) {
        asm volatile("nop");
    }
}

namespace hw::sd {

// Sends a single command to the SD Card over the CMD line.
// This requires setting up an in-memory descriptor list for the Amlogic DMA controller.
// Since we only need to read 1 block at a time for the bootloader, we can use a single descriptor.
struct sd_emmc_desc {
    uint32_t cmd_cfg;
    uint32_t cmd_arg;
    uint32_t data_addr;
    uint32_t resp_addr;
};

// Allocate a static descriptor in our BSS segment.
// In a real system, this MUST be aligned to an 8-byte boundary for the DMA hardware.
static sd_emmc_desc g_desc __attribute__((aligned(8)));

static bool send_command(uint32_t cmd_idx, uint32_t arg, uint32_t flags, uint32_t* response) {
    // 1. Build the command configuration word
    uint32_t cfg = 0;
    cfg |= (cmd_idx << CMD_CFG_CMD_INDEX_SHIFT) & CMD_CFG_CMD_INDEX_MASK;
    cfg |= flags;
    cfg |= CMD_CFG_OWNER;        // Hand control to the hardware
    cfg |= CMD_CFG_END_OF_CHAIN; // We only send one descriptor at a time

    // 2. Populate the Descriptor Memory
    g_desc.cmd_cfg = cfg;
    g_desc.cmd_arg = arg;
    g_desc.data_addr = 0; // No data transfer for basic commands yet
    g_desc.resp_addr = 0; // We will read the response directly from the MMIO register

    // 3. Clear any pending interrupts
    writel(MESON_SD_EMMC_B_BASE + SD_EMMC_STATUS, 0x3FFF);

    // 4. Point the DMA controller to our descriptor structure (must be physical address)
    // Since we are running at 0x01000000 with a flat mapping, physical == virtual.
    uint32_t desc_phys = reinterpret_cast<uint32_t>(&g_desc);
    writel(MESON_SD_EMMC_B_BASE + SD_EMMC_START, desc_phys);

    // 5. Poll the status register waiting for the command to finish
    uint32_t status;
    uint32_t timeout = 100000;
    do {
        status = readl(MESON_SD_EMMC_B_BASE + SD_EMMC_STATUS);
        timeout--;
        if (timeout == 0) return false;
    } while (!(status & (1 << 11))); // Bit 11 is End Of Chain interrupt

    // 6. Check for Error (Bit 13)
    if (status & (1 << 13)) {
        return false;
    }

    // 7. Read the response if requested
    if (response && !(flags & CMD_CFG_NO_RESP)) {
        *response = readl(MESON_SD_EMMC_B_BASE + SD_EMMC_CMD_RSP);
    }

    return true;
}

bool init() {
    // 1. Hard Reset the SD Card Controller State Machine
    writel(MESON_SD_EMMC_B_BASE + SD_EMMC_CFG, CFG_AUTO_CLK | CFG_CHK_DSYNC);
    writel(MESON_SD_EMMC_B_BASE + SD_EMMC_START, START_DESC_INIT); // Send Init sequence

    // Wait for Init sequence to complete (74 clock cycles sent to SD card to wake it up)
    delay_ms(10);

    uint32_t resp;

    // 2. Send CMD0: GO_IDLE_STATE (Reset SD Card)
    if (!send_command(0, 0, CMD_CFG_NO_RESP, nullptr)) {
        return false;
    }
    delay_ms(5);

    // 3. Send CMD8: SEND_IF_COND (Check Voltage Range)
    // Argument 0x1AA means: VHS = 2.7-3.6V (1), Check pattern = 0xAA
    if (!send_command(8, 0x1AA, 0, &resp)) {
        // If CMD8 fails, it might be an ancient SD card, but we will assume modern SDHC for our bootloader
        return false;
    }

    if ((resp & 0xFF) != 0xAA) {
        return false; // Wrong check pattern
    }

    // 4. Send ACMD41: SD_SEND_OP_COND (Initialize and check capacity)
    // We must poll this until the card says it's ready (Bit 31 is Power Up Status)
    uint32_t acmd_retries = 100;
    bool ready = false;
    while (acmd_retries-- > 0) {
        // Send CMD55 (APP_CMD) first to tell the card the next command is an ACMD
        if (!send_command(55, 0, 0, &resp)) return false;

        // Send ACMD41. Argument bit 30 = HCS (High Capacity Support, meaning SDHC/SDXC)
        if (!send_command(41, (1 << 30), 0, &resp)) return false;

        if (resp & (1 << 31)) {
            ready = true; // Card is powered up and ready
            break;
        }
        delay_ms(10);
    }

    if (!ready) return false;

    // 5. Send CMD2: ALL_SEND_CID (Get Card ID)
    if (!send_command(2, 0, CMD_CFG_RESP_128, nullptr)) return false;

    // 6. Send CMD3: SEND_RELATIVE_ADDR (Ask card to publish its new RCA address)
    if (!send_command(3, 0, 0, &resp)) return false;

    // The RCA is the top 16 bits of the response
    uint32_t rca = resp & 0xFFFF0000;

    // 7. Send CMD9: SEND_CSD (Get Card Specific Data)
    if (!send_command(9, rca, CMD_CFG_RESP_128, nullptr)) return false;

    // 8. Send CMD7: SELECT_CARD (Select the card using its RCA)
    if (!send_command(7, rca, CMD_CFG_R1B, nullptr)) return false;

    // 9. Send ACMD6: SET_BUS_WIDTH (Switch to 4-bit mode for speed)
    if (!send_command(55, rca, 0, &resp)) return false; // APP_CMD
    if (!send_command(6, 2, 0, &resp)) return false;    // Arg 2 = 4-bit mode

    // Update host controller to use 4-bit mode
    uint32_t cfg = readl(MESON_SD_EMMC_B_BASE + SD_EMMC_CFG);
    cfg &= ~CFG_BUS_WIDTH_MASK;
    cfg |= CFG_BUS_WIDTH_4BIT;
    writel(MESON_SD_EMMC_B_BASE + SD_EMMC_CFG, cfg);

    return true; // We successfully initialized the Amlogic SD Card Hardware!
}

bool read_block(uint32_t lba, uint8_t* buffer) {
    // 1. Prepare DMA Descriptor for Data Transfer
    g_desc.data_addr = reinterpret_cast<uint32_t>(buffer);

    uint32_t flags = 0;
    flags |= CMD_CFG_DATA_IO;               // This is a data command
    flags |= (512 << CMD_CFG_LENGTH_SHIFT); // We want exactly 512 bytes

    // Send CMD17: READ_SINGLE_BLOCK
    // For modern SDHC cards, the argument is the Block Number (LBA)
    return send_command(17, lba, flags, nullptr);
}

} // namespace hw::sd