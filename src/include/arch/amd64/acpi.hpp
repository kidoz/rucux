// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::amd64::acpi {

struct rsdp_descriptor {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed));

struct rsdp_descriptor_20 {
    rsdp_descriptor v1;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

// MADT (Multiple APIC Description Table)
struct madt {
    sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags; // bit 0: dual 8259 PICs installed
} __attribute__((packed));

// MADT entry header
struct madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

// Type 0: Processor Local APIC
struct madt_local_apic {
    madt_entry_header header;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags; // bit 0: enabled, bit 1: online-capable
} __attribute__((packed));

// Type 1: I/O APIC
struct madt_io_apic {
    madt_entry_header header;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t io_apic_address;
    uint32_t global_system_interrupt_base;
} __attribute__((packed));

// Type 2: Interrupt Source Override
struct madt_iso {
    madt_entry_header header;
    uint8_t bus_source;
    uint8_t irq_source;
    uint32_t global_system_interrupt;
    uint16_t flags;
} __attribute__((packed));

// Parsed MADT results
struct cpu_info {
    uint8_t apic_id;
    bool enabled;
};

struct madt_info {
    uint32_t lapic_address;
    uint32_t io_apic_address;
    uint32_t io_apic_gsi_base;
    cpu_info cpus[64];
    uint32_t cpu_count;
    madt_iso overrides[16];
    uint32_t override_count;
};

// Parse ACPI tables starting from RSDP physical address.
// Returns true if MADT was found and parsed successfully.
bool parse_madt(uintptr_t rsdp_phys, madt_info& info) noexcept;

} // namespace arch::amd64::acpi
