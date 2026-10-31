if arch == 'aarch64'
    user_linker_script = meson.current_source_dir() + '/linker_aarch64.ld'
    user_crt0 = files('lib/crt0_aarch64.S')
else
    user_linker_script = meson.current_source_dir() + '/linker.ld'
    user_crt0 = files('lib/crt0.S')
endif

user_link_args = ['-nostdlib', '-static', '-T', user_linker_script]
user_include_directories = include_directories('../libc/include', '../src/include', '../lib/include', '..')
embed_script = find_program('../scripts/embed.py')
embedded_headers = []
user_specs = product_sources == '' ? [] : product_sources.split('\n')

foreach spec : user_specs
    parts = spec.split(':')
    app = parts[0]
    app_deps = []
    app_archives = []
    app_includes = []
    if app == 'wm' or app == 'wm_client'
        if product_arch != 'amd64'
            error(app + ' requires an amd64 Wayland sysroot')
        endif
        app_includes += include_directories('../sysroot/usr/include')
        if app == 'wm'
            app_archives += files('../sysroot/usr/lib/libwayland-server.a')
        else
            app_archives += files('../sysroot/usr/lib/libwayland-client.a')
        endif
        app_archives += files('../sysroot/usr/lib/libffi.a')
    endif
    foreach archive : app_archives
        app_deps += declare_dependency(link_args: [archive.full_path()])
    endforeach

    app_exe = executable(
        app,
        [files(meson.project_source_root() + '/' + parts[1]), user_crt0, product_headers],
        include_directories: [user_include_directories, app_includes],
        dependencies: app_deps,
        link_depends: app_archives + files(user_linker_script),
        cpp_args: user_cpp_args,
        c_args: user_c_args,
        link_args: user_link_args,
        link_with: libc,
        install: false,
    )

    embedded_headers += custom_target(
        'embed_' + app,
        input: app_exe,
        output: app + '_bin.hpp',
        command: [embed_script, '@INPUT@', '@OUTPUT@', app + '_bin'],
        build_by_default: true,
    )
endforeach
