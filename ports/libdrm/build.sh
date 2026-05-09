#!/bin/bash
set -e

LIBDRM_VERSION="2.4.119"
LIBDRM_TARBALL="libdrm-${LIBDRM_VERSION}.tar.xz"
LIBDRM_URL="https://dri.freedesktop.org/libdrm/${LIBDRM_TARBALL}"
LIBDRM_DIR="${PORTS_BUILD_DIR}/libdrm-${LIBDRM_VERSION}"

if [ ! -f "${PORTS_BUILD_DIR}/${LIBDRM_TARBALL}" ]; then
    echo "Downloading ${LIBDRM_TARBALL}..."
    curl -sSL -o "${PORTS_BUILD_DIR}/${LIBDRM_TARBALL}" "${LIBDRM_URL}"
fi

if [ ! -d "${LIBDRM_DIR}" ]; then
    echo "Extracting..."
    tar -xf "${PORTS_BUILD_DIR}/${LIBDRM_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${LIBDRM_DIR}"

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

echo "Configuring libdrm..."
mkdir -p build
meson setup build \
    --cross-file cross_file.txt \
    --prefix="${SYSROOT}/usr" \
    --default-library=static \
    -Dintel=disabled \
    -Dradeon=disabled \
    -Damdgpu=disabled \
    -Dnouveau=disabled \
    -Dvmwgfx=disabled \
    -Dfreedreno=disabled \
    -Dvc4=disabled \
    -Detnaviv=disabled \
    -Dtests=false || true

echo "Compiling libdrm..."
ninja -C build

echo "Installing libdrm..."
ninja -C build install
