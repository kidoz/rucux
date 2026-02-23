// SPDX-License-Identifier: MIT
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/process/elf.hpp>
#include <lib/string.hpp>

namespace kernel::process {

using namespace memory;

uintptr_t elf::load(uintptr_t pml4, const uint8_t* data, size_t size) noexcept {
    if (size < sizeof(elf64_ehdr)) {
        kernel::print("ELF load failed: File too small.\n");
        return 0;
    }

    uintptr_t old_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(old_cr3));
    vmm::switch_to(pml4);

    const auto* ehdr = reinterpret_cast<const elf64_ehdr*>(data);

    // Validate magic number
    if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 || ehdr->e_ident[2] != ELFMAG2 ||
        ehdr->e_ident[3] != ELFMAG3) {
        kernel::print("ELF load failed: Invalid magic number.\n");
        vmm::switch_to(old_cr3);
        return 0;
    }

    // Ensure it's a 64-bit executable (class 2)
    if (ehdr->e_ident[4] != 2) {
        kernel::print("ELF load failed: Not a 64-bit ELF.\n");
        vmm::switch_to(old_cr3);
        return 0;
    }

    // Ensure it's an executable file (type 2)
    if (ehdr->e_type != 2) {
        kernel::print("ELF load failed: Not an executable file.\n");
        vmm::switch_to(old_cr3);
        return 0;
    }

    // Iterate over program headers
    const auto* phdrs = reinterpret_cast<const elf64_phdr*>(data + ehdr->e_phoff);
    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        const auto& phdr = phdrs[i];

        if (phdr.p_type == PT_LOAD) {
            // Allocate memory for this segment
            size_t pages_needed = (phdr.p_memsz + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;

            // Map the segment
            for (size_t p = 0; p < pages_needed; ++p) {
                void* page = pmm::alloc_page();
                if (!page) {
                    kernel::print("ELF load failed: Out of physical memory.\n");
                    vmm::switch_to(old_cr3);
                    return 0;
                }

                uintptr_t virt_addr = phdr.p_vaddr + (p * pmm::PAGE_SIZE);

                // Set page flags based on segment flags (R=4, W=2, X=1)
                page_flags flags = page_flags::PRESENT | page_flags::USER;
                if (phdr.p_flags & 2) flags = flags | page_flags::WRITABLE;
                // Currently ignoring NX bit implementation for simplicity in early load

                vmm::map(virt_addr, reinterpret_cast<uintptr_t>(page), flags);
            }

            uint8_t* dest = reinterpret_cast<uint8_t*>(phdr.p_vaddr);

            // Temporarily disable Write Protect in CR0 to write to RO user pages
            uint64_t cr0;
            asm volatile("mov %%cr0, %0" : "=r"(cr0));
            asm volatile("mov %0, %%cr0" : : "r"(cr0 & ~(1ULL << 16)));

            // Copy data
            if (phdr.p_filesz > 0) {
                lib::memcpy(dest, data + phdr.p_offset, phdr.p_filesz);
            }

            // Zero out remaining BSS
            if (phdr.p_memsz > phdr.p_filesz) {
                lib::memset(dest + phdr.p_filesz, 0, phdr.p_memsz - phdr.p_filesz);
            }

            // Restore Write Protect in CR0
            asm volatile("mov %0, %%cr0" : : "r"(cr0));
        }
    }

    kernel::print("ELF Loaded successfully. Entry: {}\n", reinterpret_cast<void*>(ehdr->e_entry));
    vmm::switch_to(old_cr3);
    return ehdr->e_entry;
}

} // namespace kernel::process
