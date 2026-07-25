// SPDX-License-Identifier: MIT
#include <arch/aarch64/uart.hpp>

namespace arch::aarch64 {

namespace {

// QEMU `virt` PL011.
constexpr uintptr_t PL011_DEFAULT_BASE = 0x09000000;

// Amlogic S905 (Odroid C2) AO UART. Same block the ARMv7 port drives, and the
// one firmware has already configured by the time we run.
constexpr uintptr_t MESON_AO_UART_BASE = 0xC81004C0;

// PL011: DR at +0x00, FR at +0x18, TXFF is bit 5.
constexpr uintptr_t PL011_DR_OFFSET = 0x00;
constexpr uintptr_t PL011_FR_OFFSET = 0x18;
constexpr uint32_t PL011_TXFF = 1u << 5;

// Amlogic: WFIFO at +0x00, STATUS at +0x0C, TX_FULL is bit 21.
constexpr uintptr_t MESON_WFIFO_OFFSET = 0x00;
constexpr uintptr_t MESON_STATUS_OFFSET = 0x0C;
constexpr uint32_t MESON_TX_FULL = 1u << 21;

volatile uint32_t* g_data = nullptr;
volatile uint32_t* g_status = nullptr;
uint32_t g_tx_full_mask = 0;

} // namespace

void uart::init() noexcept {
    if (g_data != nullptr)
        return; // already configured from the device tree
    init_dynamic(PL011_DEFAULT_BASE, true);
}

void uart::init_dynamic(uintptr_t base, bool is_pl011) noexcept {
    if (is_pl011) {
        g_data = reinterpret_cast<volatile uint32_t*>(base + PL011_DR_OFFSET);
        g_status = reinterpret_cast<volatile uint32_t*>(base + PL011_FR_OFFSET);
        g_tx_full_mask = PL011_TXFF;
    } else {
        g_data = reinterpret_cast<volatile uint32_t*>(base + MESON_WFIFO_OFFSET);
        g_status = reinterpret_cast<volatile uint32_t*>(base + MESON_STATUS_OFFSET);
        g_tx_full_mask = MESON_TX_FULL;
    }
}

void uart::putc(char c) noexcept {
    if (g_data == nullptr)
        return;

    while ((*g_status & g_tx_full_mask) != 0) {
        // Spin until the transmit FIFO drains.
    }
    *g_data = static_cast<uint32_t>(static_cast<unsigned char>(c));
}

void uart::write(const char* s) noexcept {
    if (s == nullptr)
        return;
    for (; *s != '\0'; ++s)
        putc(*s);
}

// Referenced by boards that need the Amlogic base before the FDT is parsed.
void uart_init_meson_ao() noexcept {
    uart::init_dynamic(MESON_AO_UART_BASE, false);
}

} // namespace arch::aarch64
