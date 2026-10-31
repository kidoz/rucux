// SPDX-License-Identifier: MIT
#include "test_harness.hpp"
#include <array>
#include <cstring>
#include <kernel/vfs/ata.hpp>

namespace {
uint8_t registers[8];
uint8_t command;
uint8_t error_command;
uint8_t error_status;
uint8_t timeout_command;
bool stuck_busy;
bool missing_drq;
bool completion_error;
bool bad_transfer;
unsigned commands, transfers, flushes, polls, identity_word;
void (*on_transfer)();
bool nested_result;
std::array<uint8_t, 1024> buffer;

void reset() {
    std::memset(registers, 0, sizeof(registers));
    command = error_command = error_status = timeout_command = 0;
    commands = transfers = flushes = polls = identity_word = 0;
    stuck_busy = missing_drq = completion_error = bad_transfer = false;
    on_transfer = nullptr;
    buffer.fill(0x37);
}
} // namespace

namespace arch::amd64 {
void outb(uint16_t port, uint8_t value) noexcept {
    if (port < 0x1F0 || port > 0x1F7) {
        bad_transfer = true;
        return;
    }
    registers[port - 0x1F0] = value;
    if (port == 0x1F7) {
        command = value;
        ++commands;
        if (command == 0xE7) ++flushes;
    }
}
uint8_t inb(uint16_t port) noexcept {
    if (port != 0x1F7) return registers[port - 0x1F0];
    ++polls;
    if (stuck_busy || (command && command == timeout_command)) return 0x80;
    if (command && command == error_command) return error_status;
    if (completion_error && command == 0x30 && transfers == registers[2]) return 0x01;
    if (missing_drq) return 0x40;
    const bool data =
        (command == 0xEC && identity_word < 256) || ((command == 0x20 || command == 0x30) && transfers < registers[2]);
    return data ? 0x48 : 0x40;
}
uint16_t inw(uint16_t port) noexcept {
    if (port != 0x1F0 || command != 0xEC || identity_word >= 256) bad_transfer = true;
    return identity_word++ == 60 ? 4096 : 0;
}
void inw_rep(uint16_t port, void* output, uint32_t words) noexcept {
    if (port != 0x1F0 || command != 0x20 || words != 256) bad_transfer = true;
    std::memset(output, 0x5A, words * 2);
    ++transfers;
}
void outw_rep(uint16_t port, const void* input, uint32_t words) noexcept {
    if (port != 0x1F0 || command != 0x30 || words != 256 || !input) bad_transfer = true;
    ++transfers;
    if (on_transfer) {
        auto callback = on_transfer;
        on_transfer = nullptr;
        callback();
    }
}
} // namespace arch::amd64
namespace kernel {
void kwrite(const char*) noexcept {}
void kputc(char) noexcept {}
namespace console {
uintptr_t lock_output() noexcept {
    return 0;
}
void unlock_output(uintptr_t) noexcept {}
} // namespace console
} // namespace kernel

using namespace kernel::vfs;
TEST(ata_success_requires_data_and_flush_completion) {
    reset();
    ASSERT(ata::write_sectors(0x1234567, 2, buffer.data()));
    ASSERT_EQ(transfers, 2U);
    ASSERT_EQ(flushes, 1U);
    ASSERT_EQ(registers[6], 0xE1);
    ASSERT_EQ(registers[3], 0x67);
    ASSERT_EQ(registers[4], 0x45);
    ASSERT_EQ(registers[5], 0x23);
    ASSERT(!bad_transfer);
}
TEST(ata_flush_error_or_device_fault_is_failure) {
    for (uint8_t status : {0x01, 0x20}) {
        reset();
        error_command = 0xE7;
        error_status = status;
        ASSERT(!ata::write_sectors(0, 1, buffer.data()));
        ASSERT_EQ(transfers, 1U);
        ASSERT_EQ(flushes, 1U);
    }
}
TEST(ata_transfer_errors_do_not_flush_or_transfer_payload) {
    reset();
    error_command = 0x30;
    error_status = 0x01;
    ASSERT(!ata::write_sectors(0, 1, buffer.data()));
    ASSERT_EQ(transfers, 0U);
    ASSERT_EQ(flushes, 0U);
}
TEST(ata_late_transfer_error_is_not_hidden_by_a_flush_command) {
    reset();
    completion_error = true;
    ASSERT(!ata::write_sectors(0, 1, buffer.data()));
    ASSERT_EQ(transfers, 1U);
    ASSERT_EQ(flushes, 0U);
}
TEST(ata_reads_complete_and_reject_controller_errors) {
    reset();
    ASSERT(ata::read_sectors(10, 2, buffer.data()));
    ASSERT_EQ(transfers, 2U);
    for (auto byte : buffer)
        ASSERT_EQ(byte, 0x5A);
    reset();
    error_command = 0x20;
    error_status = 0x20;
    ASSERT(!ata::read_sectors(0, 1, buffer.data()));
    ASSERT_EQ(transfers, 0U);
}
TEST(ata_polling_has_a_finite_budget) {
    reset();
    stuck_busy = true;
    ASSERT(!ata::write_sectors(0, 1, buffer.data()));
    ASSERT_EQ(commands, 0U);
    ASSERT(polls >= 1000000 && polls < 1000100);
    reset();
    missing_drq = true;
    ASSERT(!ata::read_sectors(0, 1, buffer.data()));
    ASSERT(polls >= 1000000 && polls < 1000100);
    reset();
    timeout_command = 0xE7;
    ASSERT(!ata::write_sectors(0, 1, buffer.data()));
    ASSERT_EQ(flushes, 1U);
    ASSERT(polls >= 1000000 && polls < 1000100);
}
TEST(ata_invalid_requests_never_touch_the_device) {
    reset();
    ASSERT(!ata::write_sectors(0, 0, buffer.data()));
    ASSERT(!ata::write_sectors(1U << 28U, 1, buffer.data()));
    ASSERT(!ata::write_sectors((1U << 28U) - 1, 2, buffer.data()));
    ASSERT(!ata::read_sectors(0, 1, nullptr));
    ASSERT_EQ(commands, 0U);
    ASSERT_EQ(polls, 0U);
}
TEST(ata_identify_checks_data_and_completion) {
    reset();
    ASSERT(ata::init());
    ASSERT_EQ(identity_word, 256U);
    ASSERT(!bad_transfer);
    reset();
    error_command = 0xEC;
    error_status = 0x01;
    ASSERT(!ata::init());
    ASSERT_EQ(identity_word, 0U);
}
TEST(ata_reentrant_commands_fail_without_corrupting_the_active_transfer) {
    reset();
    nested_result = true;
    on_transfer = [] { nested_result = ata::read_sectors(0, 1, buffer.data()); };
    ASSERT(ata::write_sectors(0, 1, buffer.data()));
    ASSERT(!nested_result);
    ASSERT_EQ(commands, 2U);
    ASSERT_EQ(flushes, 1U);
    ASSERT(!bad_transfer);
}
TEST(ata_explicit_flush_checks_completion_and_device_faults) {
    reset();
    ASSERT(ata::flush());
    ASSERT_EQ(flushes, 1U);
    ASSERT_EQ(transfers, 0U);
    for (uint8_t status : {0x01, 0x20}) {
        reset();
        error_command = 0xE7;
        error_status = status;
        ASSERT(!ata::flush());
    }
    reset();
    timeout_command = 0xE7;
    ASSERT(!ata::flush());
    ASSERT(polls >= 1000000 && polls < 1000100);
}
int main() {
    return RUN_ALL_TESTS("ATA command completion and flushes");
}
