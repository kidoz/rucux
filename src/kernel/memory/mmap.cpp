// SPDX-License-Identifier: MIT
#include <kernel/memory/mmap.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/vfs.hpp>
#include <lib/string.hpp>

// POSIX Flags (Must match sysroot <sys/mman.h>)
#define PROT_NONE 0x00
#define PROT_READ 0x01
#define PROT_WRITE 0x02
#define PROT_EXEC 0x04

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED ((void*)-1)

namespace kernel::memory {

static uintptr_t g_next_mmap_addr = 0xA000000000; // Userspace address in PML4 index 2

void* mmap_manager::sys_mmap(void* addr, size_t length, int prot, int flags, int fd, long offset) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return MAP_FAILED;

    if (length == 0) return MAP_FAILED;

    // Align length to page size
    size_t num_pages = (length + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;

    uintptr_t virt_start = reinterpret_cast<uintptr_t>(addr);

    // If no specific address requested or MAP_FIXED is not set, we find a free spot
    if (virt_start == 0 || !(flags & MAP_FIXED)) {
        virt_start = g_next_mmap_addr;
        g_next_mmap_addr += (num_pages * pmm::PAGE_SIZE);
    } else {
        // Ensure requested address is page aligned
        if (virt_start % pmm::PAGE_SIZE != 0) {
            return MAP_FAILED;
        }
    }

    page_flags pflags = page_flags::PRESENT | page_flags::USER;
    if (prot & PROT_WRITE) pflags = pflags | page_flags::WRITABLE;
    // We ignore PROT_EXEC for now, or we could handle NO_EXECUTE if not set.

    // Map the pages
    for (size_t i = 0; i < num_pages; ++i) {
        void* phys_page = pmm::alloc_page();
        if (!phys_page) {
            // Out of memory! We should unwind and unmap... but for simplicity:
            return MAP_FAILED;
        }

        lib::memset(phys_page, 0, pmm::PAGE_SIZE);
        vmm::map(virt_start + (i * pmm::PAGE_SIZE), reinterpret_cast<uintptr_t>(phys_page), pflags);
    }

    // If it's a file-backed mapping, read the file contents into the mapped memory
    if (!(flags & MAP_ANONYMOUS) && fd >= 0 && fd < 32) {
        auto& fdesc = t->fd_table[fd];
        if (fdesc.node) {
            vfs::vfs_node* node = static_cast<vfs::vfs_node*>(fdesc.node);
            if (node->ops && node->ops->read) {
                // Read from the file directly into our newly mapped virtual memory
                node->ops->read(node, offset, length, reinterpret_cast<void*>(virt_start));
            }
        } else {
            // Invalid FD
            return MAP_FAILED;
        }
    }

    return reinterpret_cast<void*>(virt_start);
}

int mmap_manager::sys_munmap(void* addr, size_t length) noexcept {
    if (length == 0 || reinterpret_cast<uintptr_t>(addr) % pmm::PAGE_SIZE != 0) {
        return -1;
    }

    size_t num_pages = (length + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
    uintptr_t virt = reinterpret_cast<uintptr_t>(addr);

    for (size_t i = 0; i < num_pages; ++i) {
        // Our current vmm::unmap does not free physical memory (it just clears the PT entry).
        // A true mmap requires reverse mapping to free the physical page, or we leak memory.
        // For bootstrap, we'll just unmap the virtual page to prevent access.
        vmm::unmap(virt + (i * pmm::PAGE_SIZE));
    }

    return 0;
}

} // namespace kernel::memory
