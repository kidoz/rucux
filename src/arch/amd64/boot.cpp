// SPDX-License-Identifier: MIT
#include <arch/amd64/console.hpp>
#include <arch/amd64/framebuffer.hpp>
#include <arch/amd64/gdt.hpp>
#include <arch/amd64/idt.hpp>
#include <arch/amd64/pic.hpp>
#include <arch/amd64/pit.hpp>
#include <arch/amd64/uart.hpp>
#include <kernel/print.hpp>
#include <lib/type_traits.hpp>
#include <stdint.h>

#include <kernel/boot_protocol.hpp>
// ... (some lines skipped, doing exactly at the include)

#include <arch/amd64/acpi.hpp>
#include <arch/amd64/apic.hpp>
#include <arch/amd64/e1000.hpp>
#include <arch/amd64/smp.hpp>
#include <arch/amd64/syscall.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/memory/heap.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/slab.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/net/netif.hpp>
#include <kernel/net/socket.hpp>
#include <kernel/pci.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/ata.hpp>
#include <kernel/vfs/fat32.hpp>
#include <kernel/vfs/ramfs.hpp>
#include <kernel/vfs/tty.hpp>
#include <kernel/vfs/vfs.hpp>
#include <knew.hpp>
#include <lib/string.hpp>

// Verification of type_traits
static_assert(lib::is_same_v<lib::int32_t, int>);
static_assert(lib::is_same_v<lib::uint64_t, unsigned long long>);
static_assert(lib::is_same_v<lib::remove_reference_t<int&>, int>);
static_assert(lib::is_same_v<lib::remove_cv_t<const volatile int>, int>);

#include <generated_embedded_apps.hpp>
#include <kernel/process/spawn.hpp>

#include <kernel/cpu/percpu.hpp>
#include <kernel/time.hpp>

// Tell the compiler to use the C calling convention for the entry point
extern "C" {

extern void jump_to_user_space(void* entry, void* stack, void* arg);

void start_product() {
    kernel::boot::start_initial_processes();
    kernel::scheduler::scheduler::exit();
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
    arch::amd64::pit::init(1000);
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

    asm volatile("cli");
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

    // Create directory structure for terminfo (ncurses needs this)
    auto* usr = kernel::vfs::ramfs::create_directory(root, "usr");
    auto* share = kernel::vfs::ramfs::create_directory(usr, "share");
    auto* terminfo = kernel::vfs::ramfs::create_directory(share, "terminfo");
    kernel::vfs::ramfs::create_directory(terminfo, "l");
    kernel::vfs::ramfs::create_directory(terminfo, "x");

    auto* bin = kernel::vfs::ramfs::create_directory(root, "bin");
    kernel::boot::populate_embedded_binaries(bin);

    // APIC + SMP initialization
    // Validate RSDP pointer: must be in low memory (<4GB) and start with "RSD PTR "
    bool rsdp_valid = false;
    if (info->acpi_rsdp && info->acpi_rsdp < 0x100000000ULL) {
        auto* rsdp_sig = reinterpret_cast<const char*>(static_cast<uintptr_t>(info->acpi_rsdp));
        rsdp_valid = (rsdp_sig[0] == 'R' && rsdp_sig[1] == 'S' && rsdp_sig[2] == 'D' && rsdp_sig[3] == ' ' &&
                      rsdp_sig[4] == 'P' && rsdp_sig[5] == 'T' && rsdp_sig[6] == 'R' && rsdp_sig[7] == ' ');
    }
    if (rsdp_valid) {
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
        kernel::print("ACPI: No RSDP, using legacy PIC for devices\n");
        uint32_t apic_lo, apic_hi;
        asm volatile("rdmsr" : "=a"(apic_lo), "=d"(apic_hi) : "c"(0x1B));
        if ((apic_lo & (1U << 11)) && !(apic_lo & (1U << 10)) && apic_hi == 0) {
            // Firmware may leave the PIT's IRQ0 route unusable after UEFI.
            // The local timer works independently of that external route.
            arch::amd64::lapic::init(apic_lo & 0xFFFFF000U);
            arch::amd64::lapic::write(arch::amd64::lapic_reg::LVT_LINT0, 0x700); // PIC ExtINT
            arch::amd64::pic::mask(0);
            arch::amd64::idt_use_lapic_timer();
            arch::amd64::lapic::timer_init(0x20, arch::amd64::calibrate_lapic_timer(), 0x03);
        }
    }

    kernel::pci::init();

    // Initialize network stack
    kernel::net::net_init();
    kernel::net::socket_manager::init();
    arch::amd64::e1000::init(); // Probe for E1000 NIC

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

    kernel::print("rucux (amd64) Complete!\n");

    // Publish the first runnable thread before timer IRQs can schedule away
    // from the boot stack. An empty queue would abandon boot for idle.
    kernel::scheduler::scheduler::spawn(start_product, 100);

    kernel::print("Transitioning to Multi-Process Scheduler...\n");
    asm volatile("sti");
    kernel::scheduler::scheduler::schedule();

    // We should not reach here
    while (true) {
        asm volatile("hlt");
    }
}

} // extern "C"
