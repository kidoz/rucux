# SPDX-License-Identifier: MIT
project(
    'rucux',
    ['cpp', 'c'],
    version: '0.1.0',
    default_options: [
        'cpp_std=c++23',
        'warning_level=3',
        'werror=true',
        'b_sanitize=none',
        'b_pie=false',
    ],
)

common_cpp_args = [
    '-ffreestanding',
    '-fno-exceptions',
    '-fno-rtti',
    '-fno-threadsafe-statics',
    '-nostdlib',
    '-nostdinc++',
    '-I' + meson.current_source_dir() + '/src/include',
    '-I' + meson.current_source_dir() + '/lib/include',
    '-I' + meson.current_source_dir(),
]
common_runtime_sources = files('lib/string.cpp')
libc_include_directories = include_directories('libc/include', 'lib/include', '.')

product_name = get_option('product')
product_arch = run_command('python3', files('tools/product_info.py'), 'get', product_name, 'arch', check: true).stdout().strip()
expected_arch = {'x86_64': 'amd64', 'arm': 'armv7', 'aarch64': 'aarch64'}
arch = host_machine.cpu_family()
if product_arch != expected_arch[arch]
    error('Product architecture ' + product_arch + ' does not match the cross file')
endif

configure_file(input: 'products/' + product_name + '/product.yaml', output: 'selected-product.yaml', copy: true)
product_board = run_command('python3', files('tools/product_info.py'), 'get', product_name, 'board_name', check: true).stdout().strip()
configure_file(input: 'boards/' + product_board + '/board.yaml', output: 'selected-board.yaml', copy: true)
product_sources = run_command('python3', files('tools/generate_product_headers.py'), '--sources', product_name, check: true).stdout().strip()
product_metadata = run_command('python3', files('tools/generate_product_headers.py'), '--metadata', product_name, check: true).stdout().strip()
product_metadata_files = []
foreach metadata : product_metadata == '' ? [] : product_metadata.split('\n')
    product_metadata_files += files(metadata)
endforeach
product_headers = custom_target(
    'product_headers',
    input: [files('products/' + product_name + '/product.yaml'), files('tools/product_info.py')],
    output: ['generated_embedded_apps.hpp', 'generated_init_config.hpp'],
    command: ['python3', files('tools/generate_product_headers.py'), '@OUTPUT0@', '@OUTPUT1@', product_name],
    depend_files: files('tools/generate_product_headers.py') + product_metadata_files,
    build_by_default: true,
)

kernel_args = []
user_c_args = []
user_cpp_args = []
subdir('src')
if arch == 'x86_64'
    subdir('src/arch/amd64')
elif arch == 'arm'
    subdir('src/arch/armv7')
elif arch == 'aarch64'
    subdir('src/arch/aarch64')
else
    error('Unsupported architecture: ' + arch)
endif

src_files = kernel_common_sources + arch_feature_sources + arch_kernel_sources
src_files += common_runtime_sources + files('lib/new.cpp')
add_project_arguments(common_cpp_args, language: 'cpp')

embedded_headers = []
if userspace_enabled
    subdir('libc')
    subdir('userspace')
endif

kernel_link_args = ['-nostdlib', '-static', '-T', kernel_linker_script, '-e', entry_point]
if arch == 'arm' or arch == 'aarch64'
    kernel_link_args += ['-lgcc']
    load_addr_cmd = run_command(
        'python3',
        meson.current_source_dir() + '/tools/product_info.py',
        'get',
        product_name,
        'board.platform.memory.kernel_load_address',
        check: false,
    )
    if load_addr_cmd.returncode() == 0
        kernel_link_args += ['-Wl,--defsym=KERNEL_LOAD_ADDRESS=' + load_addr_cmd.stdout().strip()]
    endif
endif

executable(
    'kernel.elf',
    src_files,
    embedded_headers,
    product_headers,
    install: false,
    cpp_args: kernel_args,
    link_args: kernel_link_args,
    link_depends: files(kernel_linker_script),
)
