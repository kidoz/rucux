userspace_enabled = true
entry_point = '_start'
kernel_linker_script = meson.current_source_dir() + '/linker.ld'

common_cpp_args += ['-mstrict-align']
kernel_args += ['-mgeneral-regs-only']
if product_name == 'smoke-qemu-aarch64'
    kernel_args += ['-DRUCUX_VM_SELF_TEST=1']
endif
user_c_args += ['-DRUCUX_ARCH="aarch64"']
user_cpp_args += ['-DRUCUX_ARCH="aarch64"']

board_defines = [
    ['board.platform.uart.base', 'RUCUX_UART_BASE'],
    ['board.platform.gic.dist_base', 'RUCUX_GIC_DIST_BASE'],
    ['board.platform.gic.cpu_base', 'RUCUX_GIC_CPU_BASE'],
    ['board.platform.memory.ram_base', 'RUCUX_RAM_BASE'],
    ['board.platform.memory.ram_size', 'RUCUX_RAM_SIZE'],
    ['board.platform.cpu_count', 'RUCUX_CPU_COUNT'],
]
foreach spec : board_defines
    probe = run_command('python3', meson.project_source_root() + '/tools/product_info.py', 'get', product_name, spec[0], check: false)
    if probe.returncode() == 0
        kernel_args += ['-D' + spec[1] + '=' + probe.stdout().strip()]
    endif
endforeach

uart_type_cmd = run_command('python3', meson.project_source_root() + '/tools/product_info.py', 'get', product_name, 'board.platform.uart.type', check: false)
if uart_type_cmd.returncode() == 0 and uart_type_cmd.stdout().strip() == 'pl011'
    kernel_args += ['-DRUCUX_UART_PL011=1']
else
    kernel_args += ['-DRUCUX_UART_PL011=0']
endif

psci_cmd = run_command('python3', meson.project_source_root() + '/tools/product_info.py', 'get', product_name, 'board.platform.psci.conduit', check: false)
if psci_cmd.returncode() == 0 and psci_cmd.stdout().strip() == 'smc'
    kernel_args += ['-DRUCUX_PSCI_HVC=0']
else
    kernel_args += ['-DRUCUX_PSCI_HVC=1']
endif

arch_feature_sources = kernel_ramfs_sources + kernel_fdt_sources
arch_kernel_sources = files(
    'start.S',
    'exception_vectors.S',
    'boot.cpp',
    'exception.cpp',
    'uart.cpp',
    'console.cpp',
    'gic.cpp',
    'timer.cpp',
    'vmm.cpp',
    'cpu/percpu.cpp',
    'scheduler/switch.S',
    'syscall.cpp',
    'psci.cpp',
    'smp.cpp',
    'smp.S',
    'power.cpp',
)
