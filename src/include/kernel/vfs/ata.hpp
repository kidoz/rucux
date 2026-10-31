// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace kernel::vfs::ata {

// Initialize the ATA IDE primary master drive
bool init() noexcept;

// Read 1..255 512-byte blocks using LBA28. False on invalid input, contention,
// device error/fault, or exhausted polling budget. Partial transfers may occur.
bool read_sectors(uint32_t lba, uint8_t sector_count, void* buffer) noexcept;

// Write 1..255 blocks and wait for FLUSH CACHE completion. True requires clear
// device error/fault status after both the transfer and flush. False does not
// mean that no data persisted. The physical device must honor its flush contract.
bool write_sectors(uint32_t lba, uint8_t sector_count, const void* buffer) noexcept;

// Explicit device flush, including a clear completion/error status check.
bool flush() noexcept;

} // namespace kernel::vfs::ata
