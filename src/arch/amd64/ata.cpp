// SPDX-License-Identifier: MIT
#include <arch/amd64/io.hpp>
#include <kernel/print.hpp>
#include <kernel/sync/spinlock.hpp>
#include <kernel/vfs/ata.hpp>

namespace kernel::vfs::ata {
namespace {
constexpr uint16_t DATA = 0x1F0;
constexpr uint16_t FEATURES = 0x1F1;
constexpr uint16_t COUNT = 0x1F2;
constexpr uint16_t LBA_LO = 0x1F3;
constexpr uint16_t LBA_MID = 0x1F4;
constexpr uint16_t LBA_HI = 0x1F5;
constexpr uint16_t DRIVE = 0x1F6;
constexpr uint16_t STATUS = 0x1F7;
constexpr uint32_t ERROR = 0x01;
constexpr uint32_t DATA_READY = 0x08;
constexpr uint32_t DEVICE_FAULT = 0x20;
constexpr uint32_t BUSY = 0x80;
constexpr uint32_t LBA_LIMIT = 1U << 28U;
// A finite poll budget, not a calibrated wall-clock deadline.
constexpr uint32_t POLL_LIMIT = 1000000;

atomic<bool> g_command_busy{false};
struct command_guard {
    bool acquired;
    command_guard() noexcept {
        bool expected = false;
        acquired = g_command_busy.compare_exchange_strong(expected, true, acquire, relaxed);
    }
    ~command_guard() noexcept {
        if (acquired) g_command_busy.store(false, release);
    }
    command_guard(const command_guard&) = delete;
    command_guard& operator=(const command_guard&) = delete;
};

enum class wait_kind { idle, data, complete };
bool wait_status(wait_kind kind) noexcept {
    for (uint32_t poll = 0; poll < POLL_LIMIT; ++poll) {
        const auto status = arch::amd64::inb(STATUS);
        if (status == 0 || status == 0xFF) return false;
        if (!(status & BUSY)) {
            // ERR may describe the previous command until a new command is
            // issued. Check errors on data/completion, not the initial wait.
            if (kind == wait_kind::idle) return true;
            if (status & (ERROR | DEVICE_FAULT)) return false;
            if ((kind == wait_kind::data) == !!(status & DATA_READY)) return true;
        }
        cpu_relax();
    }
    return false;
}

void issue(uint32_t lba, uint8_t sectors, uint8_t command) noexcept {
    arch::amd64::outb(DRIVE, static_cast<uint8_t>(0xE0U | ((lba >> 24U) & 0x0FU)));
    arch::amd64::outb(FEATURES, 0);
    arch::amd64::outb(COUNT, sectors);
    arch::amd64::outb(LBA_LO, static_cast<uint8_t>(lba));
    arch::amd64::outb(LBA_MID, static_cast<uint8_t>(lba >> 8U));
    arch::amd64::outb(LBA_HI, static_cast<uint8_t>(lba >> 16U));
    arch::amd64::outb(STATUS, command);
}

bool valid_request(uint32_t lba, uint8_t sectors, const void* buffer) noexcept {
    // ATA encodes 256 sectors as zero; this API intentionally accepts 1..255.
    return buffer && sectors && lba < LBA_LIMIT && sectors <= LBA_LIMIT - lba;
}
} // namespace

bool init() noexcept {
    command_guard guard;
    if (!guard.acquired) return false;
    arch::amd64::outb(DRIVE, 0xA0);
    if (!wait_status(wait_kind::idle)) return false;
    arch::amd64::outb(COUNT, 0);
    arch::amd64::outb(LBA_LO, 0);
    arch::amd64::outb(LBA_MID, 0);
    arch::amd64::outb(LBA_HI, 0);
    arch::amd64::outb(STATUS, 0xEC); // IDENTIFY
    if (!wait_status(wait_kind::idle) || arch::amd64::inb(LBA_MID) || arch::amd64::inb(LBA_HI) ||
        !wait_status(wait_kind::data))
        return false;
    uint16_t identity[256];
    for (auto& word : identity)
        word = arch::amd64::inw(DATA);
    if (!wait_status(wait_kind::complete)) return false;
    const auto sectors = identity[60] | (static_cast<uint32_t>(identity[61]) << 16U);
    kernel::print("ATA: Primary Master initialized. Capacity: {} MB\n",
                  (static_cast<uint64_t>(sectors) * 512) / (1024 * 1024));
    return true;
}

bool read_sectors(uint32_t lba, uint8_t sectors, void* buffer) noexcept {
    if (!valid_request(lba, sectors, buffer)) return false;
    command_guard guard;
    if (!guard.acquired || !wait_status(wait_kind::idle)) return false;
    issue(lba, sectors, 0x20);
    auto* output = static_cast<uint8_t*>(buffer);
    for (uint32_t i = 0; i < sectors; ++i) {
        if (!wait_status(wait_kind::data)) return false;
        arch::amd64::inw_rep(DATA, output, 256);
        output += 512;
    }
    return wait_status(wait_kind::complete);
}

bool write_sectors(uint32_t lba, uint8_t sectors, const void* buffer) noexcept {
    if (!valid_request(lba, sectors, buffer)) return false;
    command_guard guard;
    if (!guard.acquired || !wait_status(wait_kind::idle)) return false;
    issue(lba, sectors, 0x30);
    const auto* input = static_cast<const uint8_t*>(buffer);
    for (uint32_t i = 0; i < sectors; ++i) {
        if (!wait_status(wait_kind::data)) return false;
        arch::amd64::outw_rep(DATA, input, 256);
        input += 512;
    }
    if (!wait_status(wait_kind::complete)) return false;
    arch::amd64::outb(STATUS, 0xE7); // FLUSH CACHE is part of success, not optional.
    return wait_status(wait_kind::complete);
}

} // namespace kernel::vfs::ata
