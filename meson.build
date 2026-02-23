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

if arch == 'x86_64'
    common_cpp_args += [
        '-mno-red-zone',      # Critical for kernel interrupt handlers
        '-mno-mmx', '-mno-sse', '-mno-sse2' # Disable floating point in early kernel
    ]
    src_files = [
        'src/arch/amd64/boot.cpp',
        'src/arch/amd64/gdt.cpp',
        'src/arch/amd64/idt.cpp',
        'src/arch/amd64/tss.cpp',
        'src/arch/amd64/syscall.cpp',
        'src/arch/amd64/pic.cpp',
        'src/arch/amd64/pit.cpp',
        'src/arch/amd64/uart.cpp',
        'src/arch/amd64/print.cpp',
        'src/arch/amd64/pci.cpp',
        'src/arch/amd64/framebuffer.cpp',
        'src/arch/amd64/vmm.cpp',
        'src/arch/amd64/scheduler/switch.S',
        'src/kernel/memory/pmm.cpp',
        'src/kernel/memory/mmap.cpp',
        'src/kernel/memory/heap.cpp',
        'src/kernel/scheduler/scheduler.cpp',
        'src/kernel/vfs/vfs.cpp',
        'src/kernel/vfs/ramfs.cpp',
        'src/kernel/vfs/tty.cpp',
        'src/kernel/vfs/ata.cpp',
        'src/kernel/vfs/fat32.cpp',
        'src/kernel/process/elf.cpp',
        'src/kernel/ipc/ipc.cpp',
        'lib/string.cpp',
        'lib/new.cpp'
    ]
elif arch == 'arm'
    # ARMv7 specific flags (Odroid C2 is technically ARMv8/AArch64, but if targeting 32-bit ARMv7:)
    common_cpp_args += [
        '-marm',
        '-mfpu=vfpv3-d16'
    ]
    src_files = [
        'src/arch/armv7/boot.cpp',
        'src/arch/armv7/exception.cpp',
        'src/arch/armv7/syscall.cpp',
        'src/arch/armv7/uart.cpp',
        'src/arch/armv7/print.cpp',
        'src/arch/armv7/vmm.cpp',
        'src/arch/armv7/scheduler/switch.S',
        'src/kernel/memory/pmm.cpp',
        'src/kernel/memory/mmap.cpp',
        'src/kernel/scheduler/scheduler.cpp',
        'src/kernel/vfs/vfs.cpp',
        'src/kernel/process/elf.cpp',
        'src/kernel/ipc/ipc.cpp',
        'lib/string.cpp'
    ]
else
    error('Unsupported architecture: ' + arch)
endif

add_project_arguments(common_cpp_args, language: 'cpp')

# LibC (Sysroot)
libc = static_library('c',
    ['sysroot/usr/src/syscalls.cpp', 'sysroot/usr/src/socket.cpp', 'sysroot/usr/src/stdio.cpp', 
     'sysroot/usr/src/string.cpp', 'sysroot/usr/src/pthread.cpp', 'lib/string.cpp',
     'sysroot/usr/src/file.cpp', 'sysroot/usr/src/clearerr.cpp', 'sysroot/usr/src/snprintf.cpp',
     'sysroot/usr/src/fseeko.cpp', 'sysroot/usr/src/lseek.cpp', 'sysroot/usr/src/strerror.cpp',
     'sysroot/usr/src/vsprintf.cpp', 'sysroot/usr/src/sprintf.cpp', 'sysroot/usr/src/memchr.cpp',
     'sysroot/usr/src/exit.cpp', 'sysroot/usr/src/unistd_stubs.cpp', 'sysroot/usr/src/std_files.cpp',
     'sysroot/usr/src/stdlib.cpp', 'sysroot/usr/src/ctype.cpp', 'sysroot/usr/src/strrchr.cpp',
     'sysroot/usr/src/errno.cpp', 'sysroot/usr/src/time.cpp', 'sysroot/usr/src/strdup.cpp',
     'sysroot/usr/src/syslog.cpp', 'sysroot/usr/src/strchr.cpp', 'sysroot/usr/src/strncmp.cpp',
     'sysroot/usr/src/strtol.cpp', 'sysroot/usr/src/netdb.cpp', 'sysroot/usr/src/strcspn.cpp',
     'sysroot/usr/src/signal.cpp', 'sysroot/usr/src/dirent.cpp', 'sysroot/usr/src/poll.cpp',
     'sysroot/usr/src/strstr.cpp'],
    include_directories: include_directories('sysroot/usr/include', 'lib/include', '.'),
    cpp_args: ['-mcmodel=large'],
    c_args: ['-mcmodel=large'],
    install: false
)

# User-space setup
user_linker_script = meson.current_source_dir() + '/userspace/linker.ld'
user_link_args = ['-nostdlib', '-static', '-T', user_linker_script]
user_apps = ['init', 'console', 'echo', 'cat', 'ls', 'kbd', 'net']
embed_script = find_program('scripts/embed.py')
embedded_headers = []

foreach app : user_apps
    app_exe = executable(app,
        ['userspace/' + app + '/main.cpp', 'userspace/lib/crt0.S'],
        include_directories: include_directories('sysroot/usr/include', 'src/include', 'lib/include', '.'),
        cpp_args: ['-mcmodel=large'],
        c_args: ['-mcmodel=large'],
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

# The Kernel Executable
executable('kernel.elf',
    src_files,
    embedded_headers,
    install: false,
    link_args: ['-nostdlib', '-static', '-T', kernel_linker_script, '-e', 'kernel_main']
)
