// SPDX-License-Identifier: MIT
#include <arch/amd64/acpi.hpp>
#include <kernel/print.hpp>
#include <lib/string.hpp>

namespace arch::amd64::acpi {

static bool validate_checksum(const void* data, size_t length) noexcept {
    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i)
        sum += bytes[i];
    return sum == 0;
}

static bool sig_match(const char* a, const char* b, size_t len) noexcept {
    for (size_t i = 0; i < len; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

static sdt_header* find_table(uintptr_t rsdp_phys, const char* sig) noexcept {
    auto* rsdp = reinterpret_cast<rsdp_descriptor*>(rsdp_phys);

    if (!sig_match(rsdp->signature, "RSD PTR ", 8))
        return nullptr;

    if (!validate_checksum(rsdp, sizeof(rsdp_descriptor)))
        return nullptr;

    // Try XSDT first (ACPI 2.0+), fall back to RSDT
    if (rsdp->revision >= 2) {
        auto* rsdp2 = reinterpret_cast<rsdp_descriptor_20*>(rsdp_phys);
        auto* xsdt = reinterpret_cast<sdt_header*>(rsdp2->xsdt_address);
        if (sig_match(xsdt->signature, "XSDT", 4) && validate_checksum(xsdt, xsdt->length)) {
            size_t entries = (xsdt->length - sizeof(sdt_header)) / 8;
            auto* ptrs = reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(xsdt) + sizeof(sdt_header));
            for (size_t i = 0; i < entries; ++i) {
                auto* hdr = reinterpret_cast<sdt_header*>(ptrs[i]);
                if (sig_match(hdr->signature, sig, 4))
                    return hdr;
            }
        }
    }

    // RSDT (32-bit pointers)
    auto* rsdt = reinterpret_cast<sdt_header*>(static_cast<uintptr_t>(rsdp->rsdt_address));
    if (!sig_match(rsdt->signature, "RSDT", 4) || !validate_checksum(rsdt, rsdt->length))
        return nullptr;

    size_t entries = (rsdt->length - sizeof(sdt_header)) / 4;
    auto* ptrs = reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(rsdt) + sizeof(sdt_header));
    for (size_t i = 0; i < entries; ++i) {
        auto* hdr = reinterpret_cast<sdt_header*>(static_cast<uintptr_t>(ptrs[i]));
        if (sig_match(hdr->signature, sig, 4))
            return hdr;
    }

    return nullptr;
}

bool parse_madt(uintptr_t rsdp_phys, madt_info& info) noexcept {
    auto* hdr = find_table(rsdp_phys, "APIC");
    if (!hdr) {
        kernel::print("ACPI: MADT not found\n");
        return false;
    }

    auto* table = reinterpret_cast<madt*>(hdr);
    info.lapic_address = table->local_apic_address;
    info.cpu_count = 0;
    info.override_count = 0;
    info.io_apic_address = 0;
    info.io_apic_gsi_base = 0;

    // Walk MADT entries
    uintptr_t entry_addr = reinterpret_cast<uintptr_t>(table) + sizeof(madt);
    uintptr_t end = reinterpret_cast<uintptr_t>(table) + table->header.length;

    while (entry_addr < end) {
        auto* entry = reinterpret_cast<madt_entry_header*>(entry_addr);
        if (entry->length == 0) break;

        switch (entry->type) {
        case 0: { // Local APIC
            auto* lapic = reinterpret_cast<madt_local_apic*>(entry_addr);
            if (info.cpu_count < 64) {
                info.cpus[info.cpu_count].apic_id = lapic->apic_id;
                info.cpus[info.cpu_count].enabled = (lapic->flags & 1) != 0;
                info.cpu_count++;
            }
            break;
        }
        case 1: { // I/O APIC
            auto* ioapic = reinterpret_cast<madt_io_apic*>(entry_addr);
            info.io_apic_address = ioapic->io_apic_address;
            info.io_apic_gsi_base = ioapic->global_system_interrupt_base;
            break;
        }
        case 2: { // Interrupt Source Override
            auto* iso = reinterpret_cast<madt_iso*>(entry_addr);
            if (info.override_count < 16) {
                info.overrides[info.override_count] = *iso;
                info.override_count++;
            }
            break;
        }
        default:
            break;
        }

        entry_addr += entry->length;
    }

    kernel::print("ACPI: MADT parsed — {} CPUs, LAPIC=0x{x}, IOAPIC=0x{x}\n",
                  info.cpu_count,
                  reinterpret_cast<void*>(static_cast<uintptr_t>(info.lapic_address)),
                  reinterpret_cast<void*>(static_cast<uintptr_t>(info.io_apic_address)));

    return true;
}

} // namespace arch::amd64::acpi
