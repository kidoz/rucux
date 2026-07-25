# SPDX-License-Identifier: MIT
project('rucux', ['cpp', 'c'],
    version: '0.1.0',
    default_options: [
        'cpp_std=c++23',
        'warning_level=3',
        'werror=true',
        'b_sanitize=none',
        'b_pie=false'
    ]
)

# Common freestanding flags
common_cpp_args = [
    '-ffreestanding',
    '-fno-exceptions',
    '-fno-rtti',
    '-fno-threadsafe-statics',
    '-nostdlib',
    '-nostdinc++',
    '-I' + meson.current_source_dir() + '/src/include',
    '-I' + meson.current_source_dir() + '/lib/include',
    '-I' + meson.current_source_dir()
]

# Architecture-specific flags
arch = host_machine.cpu_family()
kernel_args = []
user_c_args = []
user_cpp_args = []

if arch == 'x86_64'
    kernel_args += [
        '-mno-red-zone',      # Critical for kernel interrupt handlers
        '-mno-mmx', '-mno-sse', '-mno-sse2' # Disable floating point in early kernel
    ]
    user_c_args += ['-mcmodel=large', '-DRUCUX_ARCH="amd64"']
    user_cpp_args += ['-mcmodel=large', '-DRUCUX_ARCH="amd64"']
    src_files = [
        'src/arch/amd64/boot.cpp',
        'src/arch/amd64/gdt.cpp',
        'src/arch/amd64/idt.cpp',
        'src/arch/amd64/tss.cpp',
        'src/arch/amd64/syscall.cpp',
        'src/arch/amd64/pic.cpp',
        'src/arch/amd64/pit.cpp',
        'src/arch/amd64/console.cpp',
        'src/arch/amd64/uart.cpp',
        'src/arch/amd64/pci.cpp',
        'src/arch/amd64/framebuffer.cpp',
        'src/arch/amd64/vmm.cpp',
        'src/arch/amd64/cpu/percpu.cpp',
        'src/arch/amd64/acpi.cpp',
        'src/arch/amd64/apic.cpp',
        'src/arch/amd64/smp.cpp',
        'src/arch/amd64/smp_trampoline.S',
        'src/arch/amd64/scheduler/switch.S',
        'src/kernel/memory/pmm.cpp',
        'src/kernel/memory/buddy.cpp',
        'src/kernel/memory/slab.cpp',
        'src/kernel/memory/vma.cpp',
        'src/kernel/memory/mmap.cpp',
        'src/kernel/memory/heap.cpp',
        'src/kernel/ds/rbtree.cpp',
        'src/kernel/scheduler/scheduler.cpp',
        'src/kernel/sync/futex.cpp',
        'src/kernel/sync/rcu.cpp',
        'src/kernel/console.cpp',
        'src/kernel/vfs/vfs.cpp',
        'src/kernel/vfs/ramfs.cpp',
        'src/kernel/vfs/tty.cpp',
        'src/kernel/vfs/ata.cpp',
        'src/kernel/vfs/fat32.cpp',
        'src/kernel/process/elf.cpp',
        'src/kernel/process/spawn.cpp',
        'src/kernel/process/signal.cpp',
        'src/kernel/process/credentials.cpp',
        'src/kernel/ipc/ipc.cpp',
        'src/kernel/scheduler/wait_queue.cpp',
        'src/kernel/trace.cpp',
        'src/kernel/time.cpp',
        'src/kernel/net/socket.cpp',
        'src/kernel/net/netbuf.cpp',
        'src/kernel/net/netif.cpp',
        'src/kernel/net/ethernet.cpp',
        'src/kernel/net/arp.cpp',
        'src/kernel/net/ipv4.cpp',
        'src/kernel/net/checksum.cpp',
        'src/kernel/net/tcp.cpp',
        'src/kernel/net/udp.cpp',
        'src/arch/amd64/e1000.cpp',
        'lib/string.cpp',
        'lib/new.cpp'
    ]
elif arch == 'arm'
    # ARMv7 specific flags (Odroid C2 is technically ARMv8/AArch64, but if targeting 32-bit ARMv7:)
    common_cpp_args += [
        '-marm',
        '-march=armv7-a+sec',
        '-mfpu=vfpv3-d16'
    ]
    src_files = [
        'src/arch/armv7/boot.cpp',
        'src/arch/armv7/exception.cpp',
        'src/arch/armv7/syscall.cpp',
        'src/arch/armv7/console.cpp',
        'src/arch/armv7/uart.cpp',
        'src/arch/armv7/vmm.cpp',
        'src/arch/armv7/cpu/percpu.cpp',
        'src/arch/armv7/gic.cpp',
        'src/arch/armv7/timer.cpp',
        'src/arch/armv7/psci.cpp',
        'src/arch/armv7/smp.cpp',
        'src/arch/armv7/dwmac.cpp',
        'src/arch/armv7/mali450.cpp',
        'src/arch/armv7/usb.cpp',
        'src/arch/armv7/hw_rng.cpp',
        'src/arch/armv7/watchdog.cpp',
        'src/arch/armv7/exception_vectors.S',
        'src/arch/armv7/scheduler/switch.S',
        'src/arch/armv7/start.S',
        'src/kernel/memory/pmm.cpp',
        'src/kernel/memory/buddy.cpp',
        'src/kernel/memory/slab.cpp',
        'src/kernel/memory/heap.cpp',
        'src/kernel/memory/vma.cpp',
        'src/kernel/memory/mmap.cpp',
        'src/kernel/ds/rbtree.cpp',
        'src/kernel/scheduler/scheduler.cpp',
        'src/kernel/scheduler/wait_queue.cpp',
        'src/kernel/sync/futex.cpp',
        'src/kernel/sync/rcu.cpp',
        'src/kernel/console.cpp',
        'src/kernel/vfs/vfs.cpp',
        'src/kernel/process/elf.cpp',
        'src/kernel/process/spawn.cpp',
        'src/kernel/process/credentials.cpp',
        'src/kernel/process/signal.cpp',
        'src/kernel/ipc/ipc.cpp',
        'src/kernel/trace.cpp',
        'src/kernel/time.cpp',
        'src/kernel/fdt.cpp',
        'src/kernel/net/socket.cpp',
        'src/kernel/net/netbuf.cpp',
        'src/kernel/net/netif.cpp',
        'src/kernel/net/ethernet.cpp',
        'src/kernel/net/arp.cpp',
        'src/kernel/net/ipv4.cpp',
        'src/kernel/net/checksum.cpp',
        'src/kernel/net/tcp.cpp',
        'src/kernel/net/udp.cpp',
        'lib/string.cpp',
        'lib/new.cpp'
    ]
elif arch == 'aarch64'
    # ARMv8-A / AArch64. Native execution state for the Odroid C2 (Amlogic S905).
    # -mgeneral-regs-only keeps FP/SIMD out of kernel code: CPACR_EL1 is left at
    # its reset value, so any generated SIMD would trap.
    common_cpp_args += [
        '-mgeneral-regs-only',
        '-mstrict-align'
    ]
    src_files = [
        'src/arch/aarch64/start.S',
        'src/arch/aarch64/exception_vectors.S',
        'src/arch/aarch64/boot.cpp',
        'src/arch/aarch64/exception.cpp',
        'src/arch/aarch64/uart.cpp',
        'src/arch/aarch64/console.cpp',
        'src/arch/aarch64/gic.cpp',
        'src/arch/aarch64/timer.cpp',
        'src/arch/aarch64/vmm.cpp',
        'src/arch/aarch64/cpu/percpu.cpp',
        'src/kernel/console.cpp',
        'src/kernel/fdt.cpp',
        'src/kernel/memory/pmm.cpp',
        'src/kernel/memory/buddy.cpp',
        'src/kernel/memory/slab.cpp',
        'src/kernel/memory/heap.cpp',
        'src/kernel/ds/rbtree.cpp',
        'lib/string.cpp',
        'lib/new.cpp'
    ]
else
    error('Unsupported architecture: ' + arch)
endif

add_project_arguments(common_cpp_args, language: 'cpp')

# LibC + userspace + kernel linker script — currently amd64-only.
# ARMv7 libc syscall ABI and userspace toolchain are not yet wired up;
# ARMv7 builds only the freestanding kernel.elf.
embedded_headers = []
if arch == 'x86_64'
    libc = static_library('c',
        ['libc/src/syscalls.cpp', 'libc/src/socket.cpp', 'libc/src/stdio.cpp',
         'libc/src/string.cpp', 'libc/src/pthread.cpp', 'lib/string.cpp',
         'libc/src/file.cpp', 'libc/src/clearerr.cpp', 'libc/src/snprintf.cpp',
         'libc/src/fseeko.cpp', 'libc/src/lseek.cpp', 'libc/src/strerror.cpp',
         'libc/src/vsprintf.cpp', 'libc/src/sprintf.cpp', 'libc/src/memchr.cpp',
         'libc/src/exit.cpp', 'libc/src/unistd_stubs.cpp', 'libc/src/std_files.cpp',
         'libc/src/stdlib.cpp', 'libc/src/ctype.cpp', 'libc/src/strrchr.cpp',
         'libc/src/errno.cpp', 'libc/src/time.cpp', 'libc/src/strdup.cpp',
         'libc/src/syslog.cpp', 'libc/src/strchr.cpp', 'libc/src/strncmp.cpp', 'libc/src/strlen.cpp',
         'libc/src/strtol.cpp', 'libc/src/netdb.cpp', 'libc/src/strcspn.cpp',
         'libc/src/signal.cpp', 'libc/src/dirent.cpp', 'libc/src/poll.cpp',
         'libc/src/strstr.cpp', 'libc/src/getopt.cpp', 'libc/src/locale.cpp',
         'libc/src/strings.cpp', 'libc/src/termios.cpp', 'libc/src/select.cpp',
         'libc/src/epoll.cpp', 'libc/src/inet.cpp', 'libc/src/setjmp.cpp', 'libc/src/sscanf.cpp'],
        include_directories: include_directories('libc/include', 'lib/include', '.'),
        cpp_args: user_cpp_args,
        c_args: user_c_args,
        install: false
    )

    # User-space setup
    user_linker_script = meson.current_source_dir() + '/userspace/linker.ld'
    user_link_args = ['-nostdlib', '-static', '-T', user_linker_script]
    user_apps = ['init', 'console', 'echo', 'cat', 'ls', 'kbd', 'net', 'pkg']
    embed_script = find_program('scripts/embed.py')

    foreach app : user_apps
        app_exe = executable(app,
            ['userspace/' + app + '/main.cpp', 'userspace/lib/crt0.S'],
            include_directories: include_directories('libc/include', 'src/include', 'lib/include', '.'),
            cpp_args: user_cpp_args,
            c_args: user_c_args,
            link_args: user_link_args,
            link_with: libc,
            install: false
        )

        embedded_headers += custom_target('embed_' + app,
            input: app_exe,
            output: app + '_bin.hpp',
            command: [embed_script, '@INPUT@', '@OUTPUT@', app + '_bin'],
            build_by_default: true
        )
    endforeach

    kernel_linker_script = meson.current_source_dir() + '/src/arch/amd64/linker.ld'
elif arch == 'aarch64'
    kernel_linker_script = meson.current_source_dir() + '/src/arch/aarch64/linker.ld'
else
    kernel_linker_script = meson.current_source_dir() + '/src/arch/armv7/linker.ld'
endif

entry_point = (arch == 'arm' or arch == 'aarch64') ? '_start' : 'kernel_main'
kernel_link_args = ['-nostdlib', '-static', '-T', kernel_linker_script, '-e', entry_point]
if arch == 'arm' or arch == 'aarch64'
    # ARM needs libgcc for compiler runtime helpers (64-bit divmod and friends).
    # -nostdlib excludes default link sets, so re-attach libgcc explicitly.
    kernel_link_args += ['-lgcc']

    # Both ARM ports link at an address declared by the board manifest, so the
    # same kernel sources serve QEMU and real hardware.
    product_name = get_option('product')
    load_addr_cmd = run_command('python3', meson.current_source_dir() + '/tools/product_info.py', 'get', product_name, 'board.platform.memory.kernel_load_address', check: false)
    if load_addr_cmd.returncode() == 0
        kernel_link_args += ['-Wl,--defsym=KERNEL_LOAD_ADDRESS=' + load_addr_cmd.stdout().strip()]
    endif
endif

if arch == 'aarch64'
    # Board-derived hardware defaults, used when firmware hands over no device
    # tree — which is the normal case for U-Boot `bootelf` on the Odroid C2.
    # Guessing a UART base instead would fault before the kernel can report it.
    board_defines = [
        ['board.platform.uart.base', 'RUCUX_UART_BASE'],
        ['board.platform.gic.dist_base', 'RUCUX_GIC_DIST_BASE'],
        ['board.platform.gic.cpu_base', 'RUCUX_GIC_CPU_BASE'],
        ['board.platform.memory.ram_base', 'RUCUX_RAM_BASE'],
        ['board.platform.memory.ram_size', 'RUCUX_RAM_SIZE']
    ]
    foreach spec : board_defines
        probe = run_command('python3', meson.current_source_dir() + '/tools/product_info.py', 'get', product_name, spec[0], check: false)
        if probe.returncode() == 0
            kernel_args += ['-D' + spec[1] + '=' + probe.stdout().strip()]
        endif
    endforeach

    uart_type_cmd = run_command('python3', meson.current_source_dir() + '/tools/product_info.py', 'get', product_name, 'board.platform.uart.type', check: false)
    if uart_type_cmd.returncode() == 0 and uart_type_cmd.stdout().strip() == 'pl011'
        kernel_args += ['-DRUCUX_UART_PL011=1']
    else
        kernel_args += ['-DRUCUX_UART_PL011=0']
    endif
endif

# The Kernel Executable
executable('kernel.elf',
    src_files,
    embedded_headers,
    install: false,
    cpp_args: kernel_args,
    link_args: kernel_link_args
)
