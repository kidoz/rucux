// SPDX-License-Identifier: MIT
#include <kernel/memory/mmap.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/user_access.hpp>
#include <kernel/memory/vma.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/vfs.hpp>
#include <lib/string.hpp>

// POSIX flags
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

namespace {

enum class install_result : uint8_t { mapped, already, gone };

uintptr_t page_base(uintptr_t phys) noexcept {
    return phys & ~(static_cast<uintptr_t>(pmm::PAGE_SIZE) - 1);
}

bool range_fits(uintptr_t start, size_t length) noexcept {
    return length != 0 && length <= USER_END - USER_BEGIN && user_range(start, length);
}

size_t page_count(size_t length) noexcept {
    return (length + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
}

install_result install_page(address_space* mm, uintptr_t virt, uintptr_t phys, page_flags flags) noexcept {
    irq_lock_guard guard(mm->lock);
    if (!mm->vmas.find(virt)) return install_result::gone;
    if (vmm::get_phys(virt) != 0) return install_result::already;
    // map() only writes kernel page-table pages, so this lock is not re-entered
    // from the fault handler.
    vmm::map(virt, phys, flags);
    if (vmm::get_phys(virt) == 0) return install_result::gone;
    return install_result::mapped;
}

bool populate(address_space* mm, vfs::vfs_node* node, uintptr_t virt_start, size_t pages, size_t length, int prot,
              int flags, long offset) noexcept {
    const bool anonymous = (flags & MAP_ANONYMOUS) != 0;
    const bool file_map = !anonymous && node && node->ops && node->ops->mmap;
    const bool file_read = !anonymous && node && node->ops && node->ops->read;
    if (!anonymous && !file_map && !file_read) return true;

    const page_flags pflags = flags_for_prot(static_cast<uint32_t>(prot));
    for (size_t i = 0; i < pages; ++i) {
        const uintptr_t virt = virt_start + i * pmm::PAGE_SIZE;
        if (file_map) {
            uintptr_t phys = node->ops->mmap(node, offset + (i * pmm::PAGE_SIZE));
            if (!phys) return false;
            if (install_page(mm, virt, phys, pflags) == install_result::gone) return false;
            continue;
        }

        void* page = pmm::alloc_page();
        if (!page) return false;
        lib::memset(page, 0, pmm::PAGE_SIZE);
        if (file_read) {
            size_t remaining = length - i * pmm::PAGE_SIZE;
            size_t count = remaining < pmm::PAGE_SIZE ? remaining : pmm::PAGE_SIZE;
            node->ops->read(node, offset + i * pmm::PAGE_SIZE, count, page);
        }
        install_result result = install_page(mm, virt, reinterpret_cast<uintptr_t>(page), pflags);
        if (result != install_result::mapped) pmm::free_page(page);
        if (result == install_result::gone) return false;
    }
    return true;
}

void rollback_mapping(address_space* mm, uintptr_t start, size_t pages) noexcept {
    bool owns = false;
    {
        irq_lock_guard guard(mm->lock);
        vma* region = mm->vmas.find(start);
        if (region) {
            owns = (region->flags & VMA_OWNS_FRAMES) != 0;
            region->flags &= ~VMA_OWNS_FRAMES;
        }
    }

    for (size_t i = 0; i < pages; ++i) {
        void* owned = nullptr;
        {
            irq_lock_guard guard(mm->lock);
            uintptr_t phys = vmm::get_phys(start + i * pmm::PAGE_SIZE);
            vmm::unmap(start + i * pmm::PAGE_SIZE);
            if (owns && phys) owned = reinterpret_cast<void*>(page_base(phys));
        }
        if (owned) pmm::free_page(owned);
    }

    irq_lock_guard guard(mm->lock);
    mm->vmas.remove(start, pages * pmm::PAGE_SIZE);
}

} // namespace

void* mmap_manager::sys_mmap(void* addr, size_t length, int prot, int flags, int fd, long offset) noexcept {
    auto* thread = scheduler::scheduler::current_thread();
    if (!thread || !thread->mm || length == 0 || length > USER_END - USER_BEGIN || (prot & ~7) != 0 ||
        (prot & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC))
        return MAP_FAILED;

    const size_t pages = page_count(length);
    const size_t aligned = pages * pmm::PAGE_SIZE;

    bool owns = true;
    vfs::vfs_node* node = nullptr;
    if ((flags & MAP_ANONYMOUS) == 0 && fd >= 0 && thread->fd_table && static_cast<size_t>(fd) < thread->fd_count &&
        thread->fd_table[fd].node) {
        node = static_cast<vfs::vfs_node*>(thread->fd_table[fd].node);
        if (node->ops && node->ops->mmap) owns = false;
    }

    address_space* mm = thread->mm;
    uintptr_t virt_start = reinterpret_cast<uintptr_t>(addr);
    const bool fixed = virt_start != 0 && (flags & MAP_FIXED) != 0;
    {
        irq_lock_guard guard(mm->lock);
        if (!fixed) {
            virt_start = mm->next_mmap;
            if (!range_fits(virt_start, aligned)) return MAP_FAILED;
        } else if (virt_start % pmm::PAGE_SIZE != 0 || !range_fits(virt_start, aligned)) {
            return MAP_FAILED;
        }

        bool occupied = false;
        for (size_t i = 0; i < pages; ++i) {
            if (vmm::get_phys(virt_start + i * pmm::PAGE_SIZE) != 0) {
                occupied = true;
                break;
            }
        }
        if (occupied) {
            // A present leaf here belongs to the loader, not this mapping.
            if (!fixed) mm->next_mmap = virt_start + aligned;
            return MAP_FAILED;
        }

        uint32_t vflags = static_cast<uint32_t>(flags);
        if (owns) vflags |= VMA_OWNS_FRAMES;
        if (!mm->vmas.insert(virt_start, virt_start + aligned, static_cast<uint32_t>(prot), vflags, fd, offset))
            return MAP_FAILED;
        if (!fixed) mm->next_mmap = virt_start + aligned;
    }

    if (!populate(mm, node, virt_start, pages, length, prot, flags, offset)) {
        rollback_mapping(mm, virt_start, pages);
        irq_lock_guard guard(mm->lock);
        if (!fixed && mm->next_mmap == virt_start + aligned) mm->next_mmap = virt_start;
        return MAP_FAILED;
    }
    return reinterpret_cast<void*>(virt_start);
}

int mmap_manager::sys_munmap(void* addr, size_t length) noexcept {
    auto* thread = scheduler::scheduler::current_thread();
    uintptr_t virt = reinterpret_cast<uintptr_t>(addr);
    if (!thread || !thread->mm || length == 0 || length > USER_END - USER_BEGIN || virt % pmm::PAGE_SIZE != 0)
        return -22;

    const size_t pages = page_count(length);
    const size_t aligned = pages * pmm::PAGE_SIZE;
    if (!user_range(virt, aligned)) return -22;

    address_space* mm = thread->mm;
    {
        irq_lock_guard guard(mm->lock);
        if (!mm->vmas.isolate(virt, virt + aligned)) return -12;
    }

    // Pages with no VMA are left mapped. ELF and stack frames are not in the tree,
    // and a shared file frame is unmapped here without being freed.
    for (size_t i = 0; i < pages; ++i) {
        const uintptr_t page = virt + i * pmm::PAGE_SIZE;
        void* owned = nullptr;
        {
            irq_lock_guard guard(mm->lock);
            vma* region = mm->vmas.find(page);
            if (!region) continue;
            uintptr_t phys = vmm::get_phys(page);
            const bool owns = (region->flags & VMA_OWNS_FRAMES) != 0;
            vmm::unmap(page);
            if (owns && phys) owned = reinterpret_cast<void*>(page_base(phys));
        }
        if (owned) pmm::free_page(owned);
    }

    irq_lock_guard guard(mm->lock);
    if (!mm->vmas.remove(virt, aligned)) return -12;
    return 0;
}

int mmap_manager::sys_mprotect(void* addr, size_t len, int prot) noexcept {
    auto* thread = scheduler::scheduler::current_thread();
    uintptr_t virt = reinterpret_cast<uintptr_t>(addr);
    if (!thread || !thread->mm || len == 0 || len > USER_END - USER_BEGIN || virt % pmm::PAGE_SIZE != 0 ||
        (prot & ~7) != 0 || (prot & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC))
        return -22;

    const size_t pages = page_count(len);
    const size_t aligned = pages * pmm::PAGE_SIZE;
    if (!user_range(virt, aligned)) return -22;

    address_space* mm = thread->mm;
    {
        irq_lock_guard guard(mm->lock);
        if (!mm->vmas.covers(virt, virt + aligned)) return -12;
        if (!mm->vmas.set_prot(virt, virt + aligned, static_cast<uint32_t>(prot))) return -12;
    }

    const page_flags pflags = flags_for_prot(static_cast<uint32_t>(prot));
    for (size_t i = 0; i < pages; ++i) {
        const uintptr_t page = virt + i * pmm::PAGE_SIZE;
        if (vmm::get_phys(page) == 0) continue;
        if (!vmm::protect(page, pflags)) return -22;
    }
    return 0;
}

int mmap_manager::sys_msync(void* addr, size_t length, int flags) noexcept {
    (void)addr;
    (void)length;
    (void)flags;
    return 0;
}

int mmap_manager::sys_madvise(void* addr, size_t length, int advice) noexcept {
    (void)addr;
    (void)length;
    (void)advice;
    return 0;
}

bool mmap_manager::handle_fault(uintptr_t fault_addr) noexcept {
    auto* thread = scheduler::scheduler::current_thread();
    if (!thread || !thread->mm) return false;
    fault_addr &= ~(static_cast<uintptr_t>(pmm::PAGE_SIZE) - 1);

    {
        irq_lock_guard guard(thread->mm->lock);
        vma* region = thread->mm->vmas.find(fault_addr);
        if (!region || (region->flags & VMA_OWNS_FRAMES) == 0 || vmm::get_phys(fault_addr) != 0) return false;
    }

    void* page = pmm::alloc_page();
    if (!page) return false;
    lib::memset(page, 0, pmm::PAGE_SIZE);

    bool mapped = false;
    bool resolved = false;
    {
        irq_lock_guard guard(thread->mm->lock);
        vma* region = thread->mm->vmas.find(fault_addr);
        if (region && (region->flags & VMA_OWNS_FRAMES) != 0 && vmm::get_phys(fault_addr) == 0) {
            vmm::map(fault_addr, reinterpret_cast<uintptr_t>(page), flags_for_prot(region->prot));
            mapped = vmm::get_phys(fault_addr) != 0;
            resolved = mapped;
        } else if (region && vmm::get_phys(fault_addr) != 0) {
            resolved = true;
        }
    }
    if (!mapped) pmm::free_page(page);
    return resolved;
}

} // namespace kernel::memory
