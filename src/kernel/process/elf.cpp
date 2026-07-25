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

    uintptr_t old_cr3 = vmm::get_active_page_table();
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
            // Each page is filled through the kernel's own mapping of the
            // physical frame, before it is mapped into the user address space.
            //
            // Writing through the user virtual address instead would require
            // every segment to be kernel-writable, which forces read-only
            // segments such as .text to be mapped writable. x86 papers over
            // that by clearing CR0.WP; AArch64 has no equivalent, because
            // AP[2:1] cannot encode "EL1 read-write, EL0 read-only".
            const uintptr_t page_base = phdr.p_vaddr & ~(pmm::PAGE_SIZE - 1);
            const size_t lead_in = static_cast<size_t>(phdr.p_vaddr - page_base);
            const size_t pages_needed = (lead_in + phdr.p_memsz + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;

            for (size_t p = 0; p < pages_needed; ++p) {
                void* page = pmm::alloc_page();
                if (!page) {
                    kernel::print("ELF load failed: Out of physical memory.\n");
                    vmm::switch_to(old_cr3);
                    return 0;
                }

                auto* frame = static_cast<uint8_t*>(page);
                lib::memset(frame, 0, pmm::PAGE_SIZE);

                // Offset of this page within the segment's memory image, where
                // byte 0 of the image is phdr.p_vaddr.
                const size_t page_start = (p == 0) ? 0 : (p * pmm::PAGE_SIZE - lead_in);
                const size_t frame_off = (p == 0) ? lead_in : 0;

                if (page_start < phdr.p_filesz) {
                    size_t avail = phdr.p_filesz - page_start;
                    size_t room = pmm::PAGE_SIZE - frame_off;
                    size_t chunk = (avail < room) ? avail : room;
                    lib::memcpy(frame + frame_off, data + phdr.p_offset + page_start, chunk);
                }
                // Bytes beyond p_filesz stay zero — that is the segment's BSS.

                // Segment flags: R=4, W=2, X=1. NX is still not applied; see
                // the W^X note in the roadmap.
                page_flags flags = page_flags::PRESENT | page_flags::USER;
                if (phdr.p_flags & 2) flags = flags | page_flags::WRITABLE;

                vmm::map(page_base + (p * pmm::PAGE_SIZE), reinterpret_cast<uintptr_t>(page), flags);
            }
        }
    }

    kernel::print("ELF Loaded successfully. Entry: {}\n", reinterpret_cast<void*>(ehdr->e_entry));
    vmm::switch_to(old_cr3);
    return ehdr->e_entry;
}

} // namespace kernel::process
