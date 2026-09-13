// SPDX-License-Identifier: MIT
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/user_access.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/process/elf.hpp>
#include <lib/string.hpp>

namespace kernel::process {

using namespace memory;

uintptr_t elf::load(uintptr_t pml4, const uint8_t* data, size_t size) noexcept {
#if defined(__x86_64__)
    constexpr uint16_t machine = 62;
#elif defined(__aarch64__)
    constexpr uint16_t machine = 183;
#else
    constexpr uint16_t machine = 40;
#endif
    if (!pml4 || !validate(data, size, machine)) return 0;
    // The temporary address space is not the caller's scheduled address space.
    // Keep preemption disabled until its original table has been restored.
    struct address_space_scope {
        uintptr_t flags = kernel::irq_save();
        uintptr_t previous = vmm::get_active_page_table();
        ~address_space_scope() {
            vmm::switch_to(previous);
            kernel::irq_restore(flags);
        }
    } scope;
    vmm::switch_to(pml4);
    const auto* ehdr = reinterpret_cast<const elf64_ehdr*>(data);

    // Iterate over program headers
    const auto* phdrs = reinterpret_cast<const elf64_phdr*>(data + ehdr->e_phoff);
    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        const auto& phdr = phdrs[i];

        if (phdr.p_type == PT_LOAD && phdr.p_memsz != 0) {
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

                // Segment flags: R=4, W=2, X=1.
                page_flags flags = page_flags::PRESENT | page_flags::USER;
                if (phdr.p_flags & 2) flags = flags | page_flags::WRITABLE;
                if (!(phdr.p_flags & 1)) flags = flags | page_flags::NO_EXECUTE;

                const uintptr_t address = page_base + p * pmm::PAGE_SIZE;
                vmm::map(address, reinterpret_cast<uintptr_t>(page), flags);
                if (vmm::get_phys(address) != reinterpret_cast<uintptr_t>(page)) {
                    pmm::free_page(page);
                    return 0;
                }
            }
        }
    }

    kernel::print("ELF Loaded successfully. Entry: {}\n", reinterpret_cast<void*>(ehdr->e_entry));
    return ehdr->e_entry;
}

} // namespace kernel::process
