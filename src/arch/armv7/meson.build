userspace_enabled = false
entry_point = '_start'
kernel_linker_script = meson.current_source_dir() + '/linker.ld'

common_cpp_args += [
    '-marm',
    '-march=armv7-a+sec',
    '-mfpu=vfpv3-d16',
]

arch_feature_sources = kernel_net_sources + kernel_fdt_sources
arch_kernel_sources = files(
    'boot.cpp',
    'exception.cpp',
    'syscall.cpp',
    'console.cpp',
    'uart.cpp',
    'vmm.cpp',
    'cpu/percpu.cpp',
    'gic.cpp',
    'timer.cpp',
    'psci.cpp',
    'smp.cpp',
    'dwmac.cpp',
    'mali450.cpp',
    'usb.cpp',
    'hw_rng.cpp',
    'watchdog.cpp',
    'exception_vectors.S',
    'scheduler/switch.S',
    'start.S',
)
