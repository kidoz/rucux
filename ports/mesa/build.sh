#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

MESA_VERSION="23.3.3"
MESA_TARBALL="mesa-${MESA_VERSION}.tar.xz"
MESA_URL="https://archive.mesa3d.org/mesa-${MESA_VERSION}.tar.xz"
MESA_DIR="${PORTS_BUILD_DIR}/mesa-${MESA_VERSION}"
HOST_CPU_FAMILY="x86_64"
HOST_CPU="x86_64"

case "${PRODUCT_ARCH:-amd64}" in
    amd64)
        HOST_CPU_FAMILY="x86_64"
        HOST_CPU="x86_64"
        ;;
    armv7)
        HOST_CPU_FAMILY="arm"
        HOST_CPU="armv7"
        ;;
    *)
        echo "Unsupported Mesa target architecture: ${PRODUCT_ARCH:-unknown}"
        exit 1
        ;;
esac

if [ ! -f "${PORTS_BUILD_DIR}/${MESA_TARBALL}" ]; then
    echo "Downloading ${MESA_TARBALL}..."
    curl -sSL -o "${PORTS_BUILD_DIR}/${MESA_TARBALL}" "${MESA_URL}"
fi

if [ ! -d "${MESA_DIR}" ]; then
    echo "Extracting..."
    tar -xf "${PORTS_BUILD_DIR}/${MESA_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${MESA_DIR}"

cat <<EOF > cross_file.txt
[binaries]
c = '${CC}'
cpp = '${CXX}'
ar = '${AR}'
strip = '${RANLIB}'
pkgconfig = 'pkg-config'
llvm-config = '${SYSROOT}/usr/bin/llvm-config'

[properties]
sys_root = '${SYSROOT}'
cmake_prefix_path = '${SYSROOT}/usr'
c_args = ['-isystem', '${SYSROOT}/usr/include', '-ffreestanding', '-D_GNU_SOURCE', '-D_POSIX_C_SOURCE=200809L', '-D__linux__=1']
cpp_args = ['-isystem', '${SYSROOT}/usr/include/c++/v1', '-isystem', '${SYSROOT}/usr/include', '-ffreestanding', '-D_GNU_SOURCE', '-D_POSIX_C_SOURCE=200809L', '-D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE=1', '-D__decay=_V1_decay', '-D__linux__=1']
c_link_args = ['-static', '-nostdlib', '-L${SYSROOT}/usr/lib']
cpp_link_args = ['-static', '-nostdlib', '-L${SYSROOT}/usr/lib']

[host_machine]
system = 'linux'
cpu_family = '${HOST_CPU_FAMILY}'
cpu = '${HOST_CPU}'
endian = 'little'
EOF

# Workarounds for missing functions/headers
sed -i.bak "s/struct timespec ts;/struct timespec ts = {0};/g" src/util/os_time.c || true
sed -i.bak "s/method : host_machine.system() == 'windows' ? 'auto' : 'config-tool'/method : 'auto'/g" meson.build || true
sed -i.bak "s/-pthread//g" meson.build || true

cat <<EOF > native_file.txt
[binaries]
pkgconfig = 'pkg-config'
wayland-scanner = '${SYSROOT}/usr/bin/wayland-scanner'

[built-in options]
pkg_config_path = '${SYSROOT}/usr/lib/pkgconfig'
EOF

echo "Configuring Mesa..."
mkdir -p build
mkdir -p "${SYSROOT}/usr/bin"
if [ -x "${PORTS_BUILD_DIR}/wayland-1.22.0/build-native/src/wayland-scanner" ]; then
    cp "${PORTS_BUILD_DIR}/wayland-1.22.0/build-native/src/wayland-scanner" "${SYSROOT}/usr/bin/"
fi
export PKG_CONFIG_PATH="${SYSROOT}/usr/lib/pkgconfig"
# Build only the software Vulkan driver first. Hardware Vulkan and normal WSI
# loader integration need a larger DRM/dynamic-linking surface than rucux has today.
meson setup build \
    --cross-file cross_file.txt \
    --native-file native_file.txt \
    --prefix="${SYSROOT}/usr" \
    --default-library=static \
    -Dplatforms=wayland \
    -Dgallium-drivers=swrast \
    -Dvulkan-drivers=swrast \
    -Dglx=disabled \
    -Degl=disabled \
    -Dgbm=disabled \
    -Dopengl=false \
    -Dshared-glapi=disabled \
    -Dgles1=disabled \
    -Dgles2=disabled \
    -Dllvm=enabled \
    -Dzstd=disabled || true

# Strip -pthread because x86_64-elf-gcc doesn't support it
sed -i.bak "s/-pthread//g" build/build.ninja || true

echo "Compiling Mesa..."
ninja -C build

echo "Installing Mesa..."
ninja -C build install
