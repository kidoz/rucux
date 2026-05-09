#!/bin/bash
set -e

WAYLAND_VERSION="1.22.0"
WAYLAND_TARBALL="wayland-${WAYLAND_VERSION}.tar.xz"
WAYLAND_URL="https://gitlab.freedesktop.org/wayland/wayland/-/releases/${WAYLAND_VERSION}/downloads/${WAYLAND_TARBALL}"
WAYLAND_DIR="${PORTS_BUILD_DIR}/wayland-${WAYLAND_VERSION}"

if [ ! -f "${PORTS_BUILD_DIR}/${WAYLAND_TARBALL}" ]; then
    echo "Downloading ${WAYLAND_TARBALL}..."
    curl -sSL -o "${PORTS_BUILD_DIR}/${WAYLAND_TARBALL}" "${WAYLAND_URL}"
fi

if [ ! -d "${WAYLAND_DIR}" ]; then
    echo "Extracting..."
    tar -xf "${PORTS_BUILD_DIR}/${WAYLAND_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${WAYLAND_DIR}"

echo "Configuring Wayland..."

# Wayland uses meson. We need to do a cross-build.
# We will create a cross file for meson.
cat <<EOF > cross_file.txt
[binaries]
c = '${CC}'
cpp = '${CXX}'
ar = '${AR}'
strip = '${RANLIB}'
pkgconfig = 'pkg-config'

[properties]
sys_root = '${SYSROOT}'
c_args = ['-isystem', '${SYSROOT}/usr/include', '-ffreestanding']
cpp_args = ['-isystem', '${SYSROOT}/usr/include', '-ffreestanding']
c_link_args = ['-static', '-nostdlib', '-L${SYSROOT}/usr/lib']
cpp_link_args = ['-static', '-nostdlib', '-L${SYSROOT}/usr/lib']

[host_machine]
system = 'baremetal'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF

# For wayland-scanner to run on the host, we might need a native build first.
# Fortunately, we can tell meson to just build the scanner for the build machine.

# However, Wayland build usually requires DTD validation, which we disable.
echo "Building native wayland-scanner..."
mkdir -p build-native
env -u CC -u CXX -u AR -u RANLIB -u LD -u CFLAGS -u CXXFLAGS -u LDFLAGS -u CPPFLAGS meson setup build-native \
    -Dlibraries=false \
    -Dscanner=true \
    -Ddocumentation=false \
    -Ddtd_validation=false \
    -Dtests=false \
    --default-library=static || true

if [ -f "build-native/src/wayland-scanner" ]; then
    echo "wayland-scanner built."
else
    ninja -C build-native
fi

# Patch src/meson.build to not fail when cross-compiling
sed -i.bak "s|scanner_dep = dependency('wayland-scanner', native: true, version: meson.project_version())||g" src/meson.build || true
sed -i.bak "s|wayland_scanner_for_build = find_program(scanner_dep.get_variable(pkgconfig: 'wayland_scanner'))|wayland_scanner_for_build = find_program('../build-native/src/wayland-scanner')|g" src/meson.build || true
sed -i.bak "s|subdir('egl')||g" meson.build || true

# We also disable tests and documentation.
mkdir -p build
meson setup build \
    --cross-file cross_file.txt \
    --prefix="${SYSROOT}/usr" \
    --default-library=static \
    -Ddocumentation=false \
    -Ddtd_validation=false \
    -Dtests=false \
    -Dscanner=false

# Point ninja to use the native scanner
sed -i.bak "s|COMMAND = wayland-scanner|COMMAND = ../build-native/src/wayland-scanner|g" build/build.ninja || true

# Remove -pthread as x86_64-elf-gcc does not support it
sed -i.bak "s/-pthread//g" build/build.ninja || true

echo "Compiling Wayland..."
ninja -C build

echo "Installing Wayland..."
ninja -C build install
