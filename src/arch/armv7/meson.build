userspace_enabled = false
entry_point = '_start'
kernel_linker_script = meson.current_source_dir() + '/linker.ld'

common_cpp_args += [
    '-marm',
    '-march=armv7-a+sec',
    '-mfpu=vfpv3-d16',
]

arch_feature_sources = kernel_net_sources + kernel_fdt_sources
arm_platform = custom_target('arm_platform',
    input: [files('../../../boards/' + product_board + '/board.yaml'), files('../../../products/' + product_name + '/product.yaml')],
    output: ['generated_arm_platform.hpp', 'generated_arm_platform.ld'],
    command: ['python3', files('../../../tools/generate_arm_board_support.py'), '@OUTPUT0@', '@OUTPUT1@', product_name],
    depend_files: files('../../../tools/product_info.py'),
)
if product_name == 'smoke-smp-qemu-armv7'
    kernel_args += ['-DRUCUX_ARMV7_SMP_SELF_TEST=1']
    arch_feature_sources += files('memory_probe.cpp', 'scheduler_probe.cpp')
endif
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
    'smp.S',
    'topology.cpp',
    'dwmac.cpp',
    'mali450.cpp',
    'usb.cpp',
    'hw_rng.cpp',
    'watchdog.cpp',
    'exception_vectors.S',
    'scheduler/switch.S',
    'start.S',
)
arch_kernel_sources += arm_platform[0]
