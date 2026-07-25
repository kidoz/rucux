// SPDX-License-Identifier: MIT
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/process/elf.hpp>
#include <kernel/process/spawn.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/vfs.hpp>
#include <knew.hpp>
#include <lib/string.hpp>

namespace kernel::process {

namespace {

#if defined(__x86_64__)
constexpr uintptr_t USER_STACK_BASE = 0x8000100000ULL;
#else
constexpr uintptr_t USER_STACK_BASE = 0x70000000UL;
#endif
constexpr size_t USER_STACK_SIZE = 64 * 1024;

void open_stdio(kernel::scheduler::thread* t) noexcept {
    auto* root = kernel::vfs::vfs_manager::get_root();
    if (!root || !t) return;

    auto* dev = root->ops ? root->ops->finddir(root, "dev") : nullptr;
    if (!dev || !dev->ops) return;

    auto* tty = dev->ops->finddir(dev, "tty");
    if (!tty) return;

    if (!t->ensure_fd_capacity(3)) return;

    for (int i = 0; i < 3; ++i) {
        t->fd_table[i].node = tty;
        t->fd_table[i].offset = 0;
        t->fd_table[i].flags = 2; // O_RDWR
    }
}

} // namespace

long spawn_path(const char* path, uint32_t requested_tid) noexcept {
    if (!path) return -1;

    auto* node = kernel::vfs::vfs_manager::resolve_path(path);
    if (!node || node->type != kernel::vfs::file_type::REGULAR || !node->ops || !node->ops->read) {
        kernel::print("SPAWN: {} not found\n", path);
        return -1;
    }

    if (node->length == 0) {
        kernel::print("SPAWN: {} is empty\n", path);
        return -1;
    }

    auto* image = new uint8_t[node->length];
    if (!image) {
        kernel::print("SPAWN: OOM reading {}\n", path);
        return -1;
    }

    size_t bytes_read = node->ops->read(node, 0, node->length, image);
    if (bytes_read != node->length) {
        kernel::print("SPAWN: short read on {}\n", path);
        delete[] image;
        return -1;
    }

    uintptr_t pml4 = kernel::memory::vmm::create_address_space();
    if (!pml4) {
        kernel::print("SPAWN: failed to create address space for {}\n", path);
        delete[] image;
        return -1;
    }

    uintptr_t entry = kernel::process::elf::load(pml4, image, node->length);
    delete[] image;
    if (!entry) {
        kernel::print("SPAWN: ELF load failed for {}\n", path);
        return -1;
    }

    uintptr_t old_pml4 = kernel::memory::vmm::get_active_page_table();
    kernel::memory::vmm::switch_to(pml4);

    for (size_t offset = 0; offset < USER_STACK_SIZE; offset += kernel::memory::pmm::PAGE_SIZE) {
        void* user_stack_page = kernel::memory::pmm::alloc_page();
        if (!user_stack_page) {
            kernel::memory::vmm::switch_to(old_pml4);
            kernel::print("SPAWN: failed to allocate user stack for {}\n", path);
            return -1;
        }

        kernel::memory::vmm::map(USER_STACK_BASE + offset, reinterpret_cast<uintptr_t>(user_stack_page),
                                 kernel::memory::page_flags::PRESENT | kernel::memory::page_flags::WRITABLE |
                                     kernel::memory::page_flags::USER);
    }

    kernel::memory::vmm::switch_to(old_pml4);

    auto* t = kernel::scheduler::scheduler::spawn_user(pml4, reinterpret_cast<void*>(entry),
                                                       reinterpret_cast<void*>(USER_STACK_BASE + USER_STACK_SIZE),
                                                       nullptr, requested_tid);
    if (!t) {
        kernel::print("SPAWN: spawn_user failed for {}\n", path);
        return -1;
    }

    open_stdio(t);
    return t->tid;
}

long sys_spawn(const char* path) noexcept {
    return spawn_path(path);
}

} // namespace kernel::process
