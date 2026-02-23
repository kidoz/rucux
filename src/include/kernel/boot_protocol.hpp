// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#define RUCUX_BOOT_MAGIC 0x1337B007

struct rucux_mmap_entry {
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint32_t type;
};

struct rucux_boot_info {
    uint32_t magic;

    // Memory map
    uint64_t mmap_size;
    uint64_t mmap_descriptor_size;
    rucux_mmap_entry* mmap;

    // Framebuffer
    uint64_t fb_address;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint8_t fb_bpp;

    // Additional Universal info
    uint64_t acpi_rsdp;    // Physical address of ACPI RSDP (if available)
    uint64_t smbios_table; // Physical address of SMBIOS table (if available)
    uint64_t dtb_address;  // Physical address of Device Tree Blob (if available)
};
