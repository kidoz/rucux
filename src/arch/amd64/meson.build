userspace_enabled = true
entry_point = 'kernel_main'
kernel_linker_script = meson.current_source_dir() + '/linker.ld'
if product_name == 'smoke-qemu-amd64' or product_name == 'smoke-storage-qemu-amd64'
    kernel_args += ['-DRUCUX_TLB_SELF_TEST=1']
endif

kernel_args += [
    '-mno-red-zone',
    '-mno-mmx',
    '-mno-sse',
    '-mno-sse2',
]
user_c_args += ['-mcmodel=large', '-DRUCUX_ARCH="amd64"']
user_cpp_args += ['-mcmodel=large', '-DRUCUX_ARCH="amd64"']

arch_feature_sources = kernel_net_sources + kernel_local_socket_sources + kernel_runtime_services_sources
arch_feature_sources += kernel_ramfs_sources + kernel_storage_sources
arch_kernel_sources = files(
    'boot.cpp',
    'gdt.cpp',
    'idt.cpp',
    'tss.cpp',
    'syscall.cpp',
    'pic.cpp',
    'pit.cpp',
    'power.cpp',
    'console.cpp',
    'uart.cpp',
    'pci.cpp',
    'framebuffer.cpp',
    'vmm.cpp',
    'cpu/percpu.cpp',
    'acpi.cpp',
    'apic.cpp',
    'smp.cpp',
    'smp_trampoline.S',
    'scheduler/switch.S',
    'e1000.cpp',
    'ata.cpp',
)
