// SPDX-License-Identifier: MIT
#include <arch/amd64/console.hpp>
#include <arch/amd64/framebuffer.hpp>
#include <arch/amd64/gdt.hpp>
#include <arch/amd64/idt.hpp>
#include <arch/amd64/pit.hpp>
#include <arch/amd64/uart.hpp>
#include <kernel/print.hpp>
#include <lib/type_traits.hpp>
#include <stdint.h>

#include <kernel/boot_protocol.hpp>
// ... (some lines skipped, doing exactly at the include)

#include <arch/amd64/acpi.hpp>
#include <arch/amd64/apic.hpp>
#include <arch/amd64/smp.hpp>
#include <arch/amd64/syscall.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/memory/slab.hpp>
#include <kernel/memory/heap.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/pci.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/ata.hpp>
#include <kernel/vfs/fat32.hpp>
#include <kernel/vfs/ramfs.hpp>
#include <kernel/vfs/tty.hpp>
#include <kernel/vfs/vfs.hpp>
#include <lib/string.hpp>
#include <knew.hpp>

// Verification of type_traits
static_assert(lib::is_same_v<lib::int32_t, int>);
static_assert(lib::is_same_v<lib::uint64_t, unsigned long long>);
static_assert(lib::is_same_v<lib::remove_reference_t<int&>, int>);
static_assert(lib::is_same_v<lib::remove_cv_t<const volatile int>, int>);

#include <cat_bin.hpp>
#include <console_bin.hpp>
#include <echo_bin.hpp>
#include <init_bin.hpp>
#include <kbd_bin.hpp>
#include <kernel/process/elf.hpp>
#include <ls_bin.hpp>
#include <net_bin.hpp>

#include <kernel/cpu/percpu.hpp>
#include <kernel/time.hpp>

// Tell the compiler to use the C calling convention for the entry point
extern "C" {

extern void jump_to_user_space(void* entry, void* stack, void* arg);

static void open_stdio(kernel::scheduler::thread* t) {
    auto* root = kernel::vfs::vfs_manager::get_root();
    auto* dev = root->ops->finddir(root, "dev");
    if (!dev) {
        kernel::print("open_stdio: /dev not found!\n");
        return;
    }
    auto* tty = dev->ops->finddir(dev, "tty");
    if (!tty) {
        kernel::print("open_stdio: /dev/tty not found!\n");
        return;
    }
    t->ensure_fd_capacity(3);
    for (int i = 0; i < 3; i++) {
        t->fd_table[i].node = tty;
        t->fd_table[i].offset = 0;
        t->fd_table[i].flags = 2; // O_RDWR
    }
}

void user_init() {
    uintptr_t pml4 = kernel::memory::vmm::create_address_space();
    uintptr_t entry = kernel::process::elf::load(pml4, init_bin, init_bin_size);
    if (!entry)
        while (true)
            ;

    kernel::memory::vmm::switch_to(pml4);

    void* user_stack = kernel::memory::pmm::alloc_page();
    kernel::memory::vmm::map(0x8000100000, reinterpret_cast<uintptr_t>(user_stack),
                             kernel::memory::page_flags::PRESENT | kernel::memory::page_flags::WRITABLE |
                                 kernel::memory::page_flags::USER);

    auto* t = kernel::scheduler::scheduler::current_thread();
    t->pml4_phys = pml4;
    open_stdio(t);

    jump_to_user_space(reinterpret_cast<void*>(entry), reinterpret_cast<void*>(0x8000100000 + 4096), nullptr);
}

void user_console() {
    uintptr_t pml4 = kernel::memory::vmm::create_address_space();
    uintptr_t entry = kernel::process::elf::load(pml4, console_bin, console_bin_size);
    if (!entry)
        while (true)
            ;

    kernel::memory::vmm::switch_to(pml4);

    void* user_stack = kernel::memory::pmm::alloc_page();
    kernel::memory::vmm::map(0x8000100000, reinterpret_cast<uintptr_t>(user_stack),
                             kernel::memory::page_flags::PRESENT | kernel::memory::page_flags::WRITABLE |
                                 kernel::memory::page_flags::USER);

    auto* t = kernel::scheduler::scheduler::current_thread();
    t->pml4_phys = pml4;
    open_stdio(t);

    jump_to_user_space(reinterpret_cast<void*>(entry), reinterpret_cast<void*>(0x8000100000 + 4096), nullptr);
}

void user_echo() {
    uintptr_t pml4 = kernel::memory::vmm::create_address_space();
    uintptr_t entry = kernel::process::elf::load(pml4, echo_bin, echo_bin_size);
    if (!entry)
        while (true)
            ;

    kernel::memory::vmm::switch_to(pml4);

    void* user_stack = kernel::memory::pmm::alloc_page();
    kernel::memory::vmm::map(0x8000100000, reinterpret_cast<uintptr_t>(user_stack),
                             kernel::memory::page_flags::PRESENT | kernel::memory::page_flags::WRITABLE |
                                 kernel::memory::page_flags::USER);

    auto* t = kernel::scheduler::scheduler::current_thread();
    t->pml4_phys = pml4;
    open_stdio(t);

    jump_to_user_space(reinterpret_cast<void*>(entry), reinterpret_cast<void*>(0x8000100000 + 4096), nullptr);
}

void user_kbd() {
    uintptr_t pml4 = kernel::memory::vmm::create_address_space();
    uintptr_t entry = kernel::process::elf::load(pml4, kbd_bin, kbd_bin_size);
    if (!entry)
        while (true)
            ;

    kernel::memory::vmm::switch_to(pml4);

    void* user_stack = kernel::memory::pmm::alloc_page();
    kernel::memory::vmm::map(0x8000100000, reinterpret_cast<uintptr_t>(user_stack),
                             kernel::memory::page_flags::PRESENT | kernel::memory::page_flags::WRITABLE |
                                 kernel::memory::page_flags::USER);

    auto* t = kernel::scheduler::scheduler::current_thread();
    t->pml4_phys = pml4;
    open_stdio(t);

    jump_to_user_space(reinterpret_cast<void*>(entry), reinterpret_cast<void*>(0x8000100000 + 4096), nullptr);
}

void user_net() {
    uintptr_t pml4 = kernel::memory::vmm::create_address_space();
    uintptr_t entry = kernel::process::elf::load(pml4, net_bin, net_bin_size);
    if (!entry)
        while (true)
            ;

    kernel::memory::vmm::switch_to(pml4);

    void* user_stack = kernel::memory::pmm::alloc_page();
    kernel::memory::vmm::map(0x8000100000, reinterpret_cast<uintptr_t>(user_stack),
                             kernel::memory::page_flags::PRESENT | kernel::memory::page_flags::WRITABLE |
                                 kernel::memory::page_flags::USER);

    auto* t = kernel::scheduler::scheduler::current_thread();
    t->pml4_phys = pml4;
    open_stdio(t);

    jump_to_user_space(reinterpret_cast<void*>(entry), reinterpret_cast<void*>(0x8000100000 + 4096), nullptr);
}

void thread_test() {
    kernel::print("Thread testing...\n");
    while (true) {
        kernel::scheduler::scheduler::yield();
    }
}

extern "C" uint8_t kernel_end[];

// The actual entry point called by our UEFI Bootloader
extern "C" void kernel_main(rucux_boot_info* info) {
    uint64_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 &= ~((1ULL << 20) | (1ULL << 21)); // Clear SMEP/SMAP
    asm volatile("mov %0, %%cr4" : : "r"(cr4));

    arch::amd64::uart::init();
    arch::amd64::console::init_early();
    arch::amd64::gdt_init();
    arch::amd64::idt_init();
    arch::amd64::pit::init(100);
    arch::amd64::syscall_init();

    kernel::print("rucux (amd64) Initialized (UEFI)!\n");
    kernel::print("Magic: 0x{x}, Info: {}\n", info->magic, (void*)info);

    if (info->magic == RUCUX_BOOT_MAGIC) {
        kernel::memory::pmm::memory_map_entry entries[64];
        size_t count = 0;

        size_t mmap_entries = info->mmap_size / info->mmap_descriptor_size;

        uintptr_t k_end = reinterpret_cast<uintptr_t>(kernel_end);

        for (size_t i = 0; i < mmap_entries && count < 64; ++i) {
            rucux_mmap_entry* mmap = reinterpret_cast<rucux_mmap_entry*>(reinterpret_cast<uint8_t*>(info->mmap) +
                                                                         (i * info->mmap_descriptor_size));

            entries[count].base = mmap->physical_start;
            entries[count].length = mmap->number_of_pages * 4096;
            entries[count].type = mmap->type;

            // Reserve memory below kernel_end
            if (entries[count].type == 1) {
                if (entries[count].base < k_end) {
                    uint64_t diff = k_end - entries[count].base;
                    if (entries[count].length <= diff) {
                        entries[count].type = 2; // Mark as reserved
                    } else {
                        entries[count].base = k_end;
                        entries[count].length -= diff;
                    }
                }
            }

            count++;
        }
        kernel::memory::pmm::init(entries, count);
        kernel::print("PMM Initialized: {} MB total, {} MB free\n",
                      (kernel::memory::pmm::get_total_pages() * 4096) / (1024 * 1024),
                      (kernel::memory::pmm::get_free_pages() * 4096) / (1024 * 1024));
    } else {
        kernel::print("Warning: Invalid Boot magic!\n");
    }

    kernel::memory::vmm::init();

    if (info->magic == RUCUX_BOOT_MAGIC && info->fb_address != 0) {
        kernel::print("Framebuffer Found: addr=0x{}, {}x{}x{}\n", reinterpret_cast<void*>(info->fb_address),
                      info->fb_width, info->fb_height, info->fb_bpp);
        arch::amd64::framebuffer::init(info->fb_address, info->fb_width, info->fb_height, info->fb_pitch, info->fb_bpp);
        arch::amd64::console::enable_framebuffer();
    }

    kernel::memory::heap::init();
    kernel::cpu::bsp_init();
    kernel::memory::slab_init();
    kernel::scheduler::scheduler::init();
    kernel::vfs::vfs_manager::init();
    kernel::time_manager::init();

    // Create Root FS
    auto* root = kernel::vfs::ramfs::create_root();
    kernel::vfs::vfs_manager::set_root(root);

    // Populate
    auto* dev = kernel::vfs::ramfs::create_directory(root, "dev");
    kernel::vfs::ramfs::create_file(dev, "uart", nullptr, 0); // Special handle for UART
    kernel::vfs::ramfs::attach_node(dev, kernel::vfs::tty::create());

    auto* etc = kernel::vfs::ramfs::create_directory(root, "etc");
    const uint8_t motd[] = "Welcome to rucux!\n";
    kernel::vfs::ramfs::create_file(etc, "motd", motd, sizeof(motd));

    auto* bin = kernel::vfs::ramfs::create_directory(root, "bin");
    kernel::vfs::ramfs::create_file(bin, "init", init_bin, init_bin_size);
    kernel::vfs::ramfs::create_file(bin, "console", console_bin, console_bin_size);
    kernel::vfs::ramfs::create_file(bin, "echo", echo_bin, echo_bin_size);
    kernel::vfs::ramfs::create_file(bin, "cat", cat_bin, cat_bin_size);
    kernel::vfs::ramfs::create_file(bin, "ls", ls_bin, ls_bin_size);
    kernel::vfs::ramfs::create_file(bin, "kbd", kbd_bin, kbd_bin_size);
    kernel::vfs::ramfs::create_file(bin, "net", net_bin, net_bin_size);

    // APIC + SMP initialization
    if (info->acpi_rsdp) {
        arch::amd64::acpi::madt_info madt{};
        if (arch::amd64::acpi::parse_madt(static_cast<uintptr_t>(info->acpi_rsdp), madt)) {
            arch::amd64::apic_init(madt);

            // Calibrate and start LAPIC timer (replaces PIT for BSP)
            uint32_t lapic_count = arch::amd64::calibrate_lapic_timer();
            // Vector 0x20 = same as PIT IRQ0, divide by 16
            arch::amd64::lapic::timer_init(0x20, lapic_count, 0x03);

            // Boot APs
            uintptr_t pml4 = kernel::memory::vmm::get_active_page_table();
            arch::amd64::smp_boot_aps(madt, pml4);
        }
    } else {
        kernel::print("ACPI: No RSDP, using legacy PIC\n");
    }

    kernel::pci::init();

    if (kernel::vfs::ata::init()) {
        uint8_t sector[512];
        if (kernel::vfs::ata::read_sectors(0, 1, sector)) {
            if (sector[510] == 0x55 && sector[511] == 0xAA) {
                uint32_t partition1_start = *(uint32_t*)(&sector[454]);
                auto* fat32_root = kernel::vfs::fat32::mount(partition1_start);
                if (fat32_root) {
                    fat32_root->name = "fat32";
                    fat32_root->name_hash = kernel::vfs::vfs_node::hash_name("fat32");
                    kernel::vfs::ramfs::attach_node(root, fat32_root);
                }
            }
        }
    }

    // Test dynamic allocation
    int* test_int = new int(123);
    kernel::print("Dynamic test: value at {} is {}\n", test_int, *test_int);
    delete test_int;

    kernel::ipc::message msg = {0, 1, {0, 0, 0, 0}};
    kernel::ipc::ipc_manager::send_sync(1, msg);

    kernel::print("rucux (amd64) Complete!\n");

    // Spawn User Space
    kernel::scheduler::scheduler::spawn(user_init, 1);
    kernel::scheduler::scheduler::spawn(user_console, 2);
    kernel::scheduler::scheduler::spawn(user_net, 5);
    kernel::scheduler::scheduler::spawn(user_echo, 3);
    kernel::scheduler::scheduler::spawn(user_kbd, 4);

    kernel::print("Transitioning to Multi-Process Scheduler...\n");
    kernel::scheduler::scheduler::schedule();

    // We should not reach here
    while (true) {
        asm volatile("hlt");
    }
}

} // extern "C"
