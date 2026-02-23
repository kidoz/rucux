// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace kernel::vfs::ata {

// Initialize the ATA IDE primary master drive
bool init() noexcept;

// Read 512-byte blocks from the drive
bool read_sectors(uint32_t lba, uint8_t sector_count, void* buffer) noexcept;

// Write 512-byte blocks to the drive
bool write_sectors(uint32_t lba, uint8_t sector_count, const void* buffer) noexcept;

} // namespace kernel::vfs::ata
