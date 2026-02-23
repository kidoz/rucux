// SPDX-License-Identifier: MIT
#include "drivers/fat32.hpp"
#include "drivers/sd.hpp"
#include <kernel/boot_protocol.hpp>
#include <stdint.h>

// Minimal ELF structures for the bootloader
struct elf64_ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

// Amlogic S905 UART0 Memory Mapped I/O address
#define UART0_WFIFO 0xC81004C0
#define UART0_STATUS 0xC81004CC

static inline void uart_putc(char c) {
    volatile uint32_t* status = (volatile uint32_t*)UART0_STATUS;
    volatile uint32_t* wfifo = (volatile uint32_t*)UART0_WFIFO;

    // Wait until TX FIFO is not full (bit 21 is TX_FULL)
    while ((*status & (1 << 21))) {
        // Spin
    }
    *wfifo = c;
}

static void uart_print(const char* str) {
    while (*str) {
        if (*str == '\n') uart_putc('\r');
        uart_putc(*str++);
    }
}

static void uart_print_hex(uint32_t val) {
    uart_print("0x");
    for (int i = 7; i >= 0; i--) {
        uint32_t nibble = (val >> (i * 4)) & 0xF;
        if (nibble < 10)
            uart_putc('0' + nibble);
        else
            uart_putc('A' + (nibble - 10));
    }
}

extern "C" {

void baremetal_main() {
    // 1. Initialize our basic UART to prove we are alive
    uart_print("\n============================================\n");
    uart_print("RUCUX BARE-METAL BL33 BOOTLOADER STARTED!\n");
    uart_print("============================================\n");

    // 2. Initialize the SD Card Controller
    uart_print("Initializing Amlogic S905 SDIO Controller...\n");
    if (!hw::sd::init()) {
        uart_print("ERROR: Failed to initialize SD Card!\n");
        while (true)
            asm volatile("wfi");
    }
    uart_print("SD Card Hardware Initialized Successfully!\n");

    // 3. Test reading the Master Boot Record (MBR) from Sector 0
    uint8_t sector[512];
    uart_print("Attempting to read LBA 0 (MBR)...\n");
    if (!hw::sd::read_block(0, sector)) {
        uart_print("ERROR: Failed to read Sector 0!\n");
        while (true)
            asm volatile("wfi");
    }

    // Check for MBR Signature (0xAA55 at the end of the sector)
    if (sector[510] == 0x55 && sector[511] == 0xAA) {
        uart_print("SUCCESS: Valid MBR Signature Found!\n");
        // Print the start sector of the first partition
        uint32_t partition1_start = *(uint32_t*)(&sector[454]);
        uart_print("Partition 1 starts at LBA: ");
        uart_print_hex(partition1_start);
        uart_print("\n");

        uart_print("Initializing FAT32...\n");
        if (!fs::fat32::init(partition1_start)) {
            uart_print("ERROR: Failed to initialize FAT32 filesystem!\n");
            while (true)
                asm volatile("wfi");
        }

        fs::fat32::file_info kernel_info;
        uart_print("Searching for KERNEL  ELF...\n");
        if (!fs::fat32::find_file("KERNEL  ELF", kernel_info)) {
            uart_print("ERROR: Could not find kernel.elf!\n");
            while (true)
                asm volatile("wfi");
        }

        uart_print("Found KERNEL  ELF, Size: ");
        uart_print_hex(kernel_info.size_bytes);
        uart_print("\nLoading into memory at 0x10000000...\n");

        uint8_t* elf_buffer = reinterpret_cast<uint8_t*>(0x10000000);
        if (!fs::fat32::read_file(kernel_info, elf_buffer)) {
            uart_print("ERROR: Failed to read kernel.elf!\n");
            while (true)
                asm volatile("wfi");
        }
        uart_print("Kernel loaded to RAM. Parsing ELF...\n");

        auto* ehdr = reinterpret_cast<elf64_ehdr*>(elf_buffer);
        if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
            uart_print("ERROR: Invalid ELF Magic!\n");
            while (true)
                asm volatile("wfi");
        }

        auto* phdrs = reinterpret_cast<elf64_phdr*>(elf_buffer + ehdr->e_phoff);
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdrs[i].p_type == 1) { // PT_LOAD
                uint8_t* dest = reinterpret_cast<uint8_t*>(phdrs[i].p_paddr);
                for (uint64_t j = 0; j < phdrs[i].p_memsz; j++)
                    dest[j] = 0;

                if (phdrs[i].p_filesz > 0) {
                    uint8_t* src = elf_buffer + phdrs[i].p_offset;
                    for (uint64_t j = 0; j < phdrs[i].p_filesz; j++)
                        dest[j] = src[j];
                }
            }
        }

        uart_print("ELF Segments Mapped. Preparing rucux_boot_info...\n");

        // Setup simple boot info
        auto* boot_info = reinterpret_cast<rucux_boot_info*>(0x11000000);
        boot_info->magic = RUCUX_BOOT_MAGIC;
        boot_info->mmap_size = 0;
        boot_info->mmap = nullptr;
        boot_info->fb_address = 0;

        uart_print("Jumping to kernel_main...\n");

        typedef void (*KernelEntry)(rucux_boot_info*);
        KernelEntry kernel = reinterpret_cast<KernelEntry>(ehdr->e_entry);
        kernel(boot_info);

    } else {
        uart_print("WARNING: Invalid MBR Signature.\n");
    }

    uart_print("\nHalting CPU.\n");

    while (true) {
        asm volatile("wfi");
    }
}

// The absolute first instruction executed by the CPU when BL31 jumps to us
__attribute__((section(".text.boot"))) void _start() {
    // 1. Set up the C stack pointer
    extern uint32_t __stack_top;
    asm volatile("ldr sp, =%0" : : "i"(&__stack_top));

    // 2. Clear the BSS section
    extern uint32_t __bss_start;
    extern uint32_t __bss_end;
    for (uint32_t* p = &__bss_start; p < &__bss_end; p++) {
        *p = 0;
    }

    // 3. Jump to C++ code
    baremetal_main();
}

} // extern "C"
