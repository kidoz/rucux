// SPDX-License-Identifier: MIT
#include <kernel/memory/user_access.hpp>
#include <kernel/process/elf.hpp>

namespace kernel::process {

bool elf::validate(const uint8_t* data, size_t size, uint16_t machine) noexcept {
    if (!data || size < sizeof(elf64_ehdr)) return false;
    const auto& h = *reinterpret_cast<const elf64_ehdr*>(data);
    if (h.e_ident[0] != ELFMAG0 || h.e_ident[1] != ELFMAG1 || h.e_ident[2] != ELFMAG2 || h.e_ident[3] != ELFMAG3 ||
        h.e_ident[4] != 2 || h.e_ident[5] != 1 || h.e_ident[6] != 1 || h.e_version != 1 || h.e_type != 2 ||
        h.e_machine != machine || h.e_ehsize != sizeof(h) || h.e_phentsize != sizeof(elf64_phdr) || h.e_phnum == 0 ||
        h.e_phnum > 128 || h.e_phoff > size || h.e_phnum > (size - h.e_phoff) / sizeof(elf64_phdr))
        return false;

    const auto* ph = reinterpret_cast<const elf64_phdr*>(data + h.e_phoff);
    bool executable_entry = false;
    for (uint16_t i = 0; i < h.e_phnum; ++i) {
        const auto& p = ph[i];
        if (p.p_type != PT_LOAD) continue;
        if (p.p_memsz == 0) {
            if (p.p_filesz != 0)
                return false;
            else
                continue;
        }
        if (p.p_filesz > p.p_memsz || p.p_offset > size || p.p_filesz > size - p.p_offset ||
            p.p_vaddr < memory::USER_BEGIN || p.p_vaddr >= memory::USER_END ||
            p.p_memsz > memory::USER_END - p.p_vaddr || (p.p_flags & ~7U) || (p.p_flags & 3U) == 3U)
            return false;
        if (p.p_align > 1 && ((p.p_align & (p.p_align - 1)) || (p.p_vaddr % p.p_align != p.p_offset % p.p_align)))
            return false;
        if (p.p_memsz == 0) continue;
        if ((p.p_flags & 1) && h.e_entry >= p.p_vaddr && h.e_entry - p.p_vaddr < p.p_filesz) executable_entry = true;

        // Independently allocated segments must not replace each other's pages.
        const uint64_t begin = p.p_vaddr & ~4095ULL;
        const uint64_t end = (p.p_vaddr + p.p_memsz + 4095) & ~4095ULL;
        for (uint16_t j = 0; j < i; ++j) {
            const auto& prev = ph[j];
            if (prev.p_type != PT_LOAD || prev.p_memsz == 0) continue;
            const uint64_t prev_begin = prev.p_vaddr & ~4095ULL;
            const uint64_t prev_end = (prev.p_vaddr + prev.p_memsz + 4095) & ~4095ULL;
            if (begin < prev_end && prev_begin < end) return false;
        }
    }
    return executable_entry;
}

} // namespace kernel::process
