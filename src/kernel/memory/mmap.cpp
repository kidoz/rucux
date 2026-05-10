// SPDX-License-Identifier: MIT
#include <kernel/memory/mmap.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vma.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/vfs.hpp>
#include <lib/string.hpp>

// POSIX Flags
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

#if defined(__LP64__) || defined(__x86_64__) || defined(__aarch64__)
static uintptr_t g_next_mmap_addr = 0xA000000000;
#else
static uintptr_t g_next_mmap_addr = 0x40000000;
#endif

// Per-address-space VMA manager.
// In a full implementation each process would have its own; for now, global.
// Accessible from vmm.cpp for demand paging.
vma_manager g_vma;
bool g_vma_initialized = false;

static void ensure_vma_init() noexcept {
    if (!g_vma_initialized) {
        g_vma.init();
        g_vma_initialized = true;
    }
}

void* mmap_manager::sys_mmap(void* addr, size_t length, int prot, int flags, int fd, long offset) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t || length == 0) return MAP_FAILED;

    ensure_vma_init();

    size_t num_pages = (length + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
    size_t aligned_len = num_pages * pmm::PAGE_SIZE;

    uintptr_t virt_start = reinterpret_cast<uintptr_t>(addr);
    if (virt_start == 0 || !(flags & MAP_FIXED)) {
        virt_start = g_next_mmap_addr;
        g_next_mmap_addr += aligned_len;
    } else if (virt_start % pmm::PAGE_SIZE != 0) {
        return MAP_FAILED;
    }

    // Create VMA to track this mapping
    g_vma.insert(virt_start, virt_start + aligned_len, static_cast<uint32_t>(prot),
                 static_cast<uint32_t>(flags), fd, offset);

    page_flags pflags = page_flags::PRESENT | page_flags::USER;
    if (prot & PROT_WRITE) pflags = pflags | page_flags::WRITABLE;

    // For anonymous mappings: allocate and map pages immediately
    // (Demand paging will be added in the VMM task)
    if (flags & MAP_ANONYMOUS) {
        for (size_t i = 0; i < num_pages; ++i) {
            void* phys = pmm::alloc_page();
            if (!phys) return MAP_FAILED;
            lib::memset(phys, 0, pmm::PAGE_SIZE);
            vmm::map(virt_start + (i * pmm::PAGE_SIZE),
                     reinterpret_cast<uintptr_t>(phys), pflags);
        }
    } else if (fd >= 0 && static_cast<size_t>(fd) < t->fd_count) {
        // File-backed mapping
        auto& fdesc = t->fd_table[fd];
        if (fdesc.node) {
            vfs::vfs_node* node = static_cast<vfs::vfs_node*>(fdesc.node);
            if (node->ops && node->ops->mmap) {
                for (size_t i = 0; i < num_pages; ++i) {
                    uintptr_t phys = node->ops->mmap(node, offset + (i * pmm::PAGE_SIZE));
                    if (phys) {
                        vmm::map(virt_start + (i * pmm::PAGE_SIZE), phys, pflags);
                    }
                }
            } else if (node->ops && node->ops->read) {
                // No mmap support — alloc pages and read into them
                for (size_t i = 0; i < num_pages; ++i) {
                    void* phys = pmm::alloc_page();
                    if (!phys) return MAP_FAILED;
                    lib::memset(phys, 0, pmm::PAGE_SIZE);
                    vmm::map(virt_start + (i * pmm::PAGE_SIZE),
                             reinterpret_cast<uintptr_t>(phys), pflags);
                }
                node->ops->read(node, offset, length, reinterpret_cast<void*>(virt_start));
            }
        }
    }

    return reinterpret_cast<void*>(virt_start);
}

int mmap_manager::sys_munmap(void* addr, size_t length) noexcept {
    uintptr_t virt = reinterpret_cast<uintptr_t>(addr);
    if (length == 0 || virt % pmm::PAGE_SIZE != 0) return -1;

    size_t num_pages = (length + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;

    // Unmap pages and free physical memory
    for (size_t i = 0; i < num_pages; ++i) {
        uintptr_t va = virt + (i * pmm::PAGE_SIZE);
        uintptr_t phys = vmm::get_phys(va);
        vmm::unmap(va);
        if (phys) {
            pmm::free_page(reinterpret_cast<void*>(phys));
        }
    }

    // Remove VMA tracking
    ensure_vma_init();
    g_vma.remove(virt, num_pages * pmm::PAGE_SIZE);

    return 0;
}

int mmap_manager::sys_mprotect(void* addr, size_t len, int prot) noexcept {
    (void)addr; (void)len; (void)prot;
    return 0; // Stub — would update PTE flags via VMA lookup
}

int mmap_manager::sys_msync(void* addr, size_t length, int flags) noexcept {
    (void)addr; (void)length; (void)flags;
    return 0; // Stub — would flush dirty pages via VMA's fd
}

int mmap_manager::sys_madvise(void* addr, size_t length, int advice) noexcept {
    (void)addr; (void)length; (void)advice;
    return 0;
}

} // namespace kernel::memory
