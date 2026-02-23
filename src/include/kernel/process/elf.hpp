// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::process {

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
} __attribute__((packed));

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed));

class elf {
public:
    static constexpr uint8_t ELFMAG0 = 0x7F;
    static constexpr uint8_t ELFMAG1 = 'E';
    static constexpr uint8_t ELFMAG2 = 'L';
    static constexpr uint8_t ELFMAG3 = 'F';

    static constexpr uint32_t PT_LOAD = 1;

    // Load an ELF executable into memory and return the entry point address.
    // Returns 0 on failure.
    static uintptr_t load(uintptr_t pml4, const uint8_t* data, size_t size) noexcept;
};

} // namespace kernel::process
