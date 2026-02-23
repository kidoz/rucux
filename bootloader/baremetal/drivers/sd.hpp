// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace hw::sd {

// Based on reverse-engineering the U-Boot and Linux 'meson-gx' drivers for Amlogic S905 (GXBB).
// The S905 has 3 SDIO host controllers:
// SDIO_A (SDIO / WiFi)   : 0xC8108420 (approx)
// SDIO_B (SD Card)       : 0xD0074000
// SDIO_C (eMMC)          : 0xD0072000
#define MESON_SD_EMMC_B_BASE 0xD0074000

// Register Offsets (from the base address)
#define SD_EMMC_CLOCK 0x00
#define SD_EMMC_DELAY1 0x04
#define SD_EMMC_DELAY2 0x08
#define SD_EMMC_ADJUST 0x0C
#define SD_EMMC_CALOUT 0x10
#define SD_EMMC_START 0x40
#define SD_EMMC_CFG 0x44
#define SD_EMMC_STATUS 0x48
#define SD_EMMC_IRQ_EN 0x4C
#define SD_EMMC_CMD_CFG 0x50
#define SD_EMMC_CMD_ARG 0x54
#define SD_EMMC_CMD_DAT 0x58
#define SD_EMMC_CMD_RSP 0x5C
#define SD_EMMC_CMD_RSP1 0x60
#define SD_EMMC_CMD_RSP2 0x64
#define SD_EMMC_CMD_RSP3 0x68

// Bit definitions for SD_EMMC_START
#define START_DESC_INIT (1 << 0)
#define START_DESC_BUSY (1 << 1)
#define START_DESC_ERROR (1 << 2)

// Bit definitions for SD_EMMC_CFG
#define CFG_BUS_WIDTH_MASK (3 << 0)
#define CFG_BUS_WIDTH_1BIT (0 << 0)
#define CFG_BUS_WIDTH_4BIT (1 << 0)
#define CFG_BUS_WIDTH_8BIT (2 << 0)
#define CFG_DDR (1 << 2)
#define CFG_BLK_LEN_SHIFT 16
#define CFG_BLK_LEN_MASK (0xF << 16)
#define CFG_RC_CC_MASK (0xF << 20) // Response timeout
#define CFG_CHK_DSYNC (1 << 24)
#define CFG_IGNORE_ERROR (1 << 25)
#define CFG_AUTO_CLK (1 << 26) // Automatically gate clock when idle

// Bit definitions for SD_EMMC_CMD_CFG
#define CMD_CFG_LENGTH_SHIFT 0
#define CMD_CFG_LENGTH_MASK 0x1FF
#define CMD_CFG_BLOCK_MODE (1 << 9)
#define CMD_CFG_R1B (1 << 10)
#define CMD_CFG_END_OF_CHAIN (1 << 11)
#define CMD_CFG_TIMEOUT_SHIFT 12
#define CMD_CFG_TIMEOUT_MASK (0xF << 12)
#define CMD_CFG_NO_RESP (1 << 16)
#define CMD_CFG_NO_CMD (1 << 17)
#define CMD_CFG_DATA_IO (1 << 18)
#define CMD_CFG_DATA_WR (1 << 19)
#define CMD_CFG_RESP_NOCRC (1 << 20)
#define CMD_CFG_RESP_128 (1 << 21)
#define CMD_CFG_RESP_NUM (1 << 22)
#define CMD_CFG_DATA_NUM (1 << 23)
#define CMD_CFG_CMD_INDEX_SHIFT 24
#define CMD_CFG_CMD_INDEX_MASK (0x3F << 24)
#define CMD_CFG_ERROR (1 << 30)
#define CMD_CFG_OWNER (1 << 31) // 1 = Hardware owns descriptor, 0 = Software

// Helper for MMIO Access
static inline void writel(uint32_t addr, uint32_t val) {
    *(volatile uint32_t*)(addr) = val;
}

static inline uint32_t readl(uint32_t addr) {
    return *(volatile uint32_t*)(addr);
}

// SD Controller Interface
bool init();
bool read_block(uint32_t lba, uint8_t* buffer);

} // namespace hw::sd
