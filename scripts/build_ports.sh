#!/bin/bash
set -e

# Rucux Ports System - Master Builder
# Cross-compiles 3rd party software targeting the rucux microkernel sysroot

# 1. Setup Environment
export RUCUX_ROOT=$(cd "$(dirname "$0")/.." && pwd)
export SYSROOT="${RUCUX_ROOT}/sysroot"

# Toolchain (AMD64 by default for now)
export CC="x86_64-elf-gcc"
export CXX="x86_64-elf-g++"
export AR="x86_64-elf-ar"
export RANLIB="x86_64-elf-ranlib"
export LD="x86_64-elf-ld"

# Flags to force compilation against our custom sysroot and freestanding environment
export CFLAGS="-ffreestanding -fno-exceptions -fno-rtti -mcmodel=large -I${SYSROOT}/usr/include -I${RUCUX_ROOT}/libc/include -I${RUCUX_ROOT}/lib/include -I${RUCUX_ROOT}/src/include"
export CXXFLAGS="${CFLAGS} -std=c++23"
export LDFLAGS="-nostdlib -static -L${SYSROOT}/usr/lib"
export LIBS="${SYSROOT}/usr/lib/libc.a"

# The master build directory for ports
export PORTS_BUILD_DIR="${RUCUX_ROOT}/builddir/ports"
mkdir -p "${PORTS_BUILD_DIR}"

# Ensure libc is in the sysroot
mkdir -p "${SYSROOT}/usr/lib"
mkdir -p "${SYSROOT}/usr/include"

# Copy standard libc headers to sysroot so ports can use them
cp -r "${RUCUX_ROOT}/libc/include/"* "${SYSROOT}/usr/include/"

# Meson creates thin archives by default which breaks external toolchains. We must convert it.
x86_64-elf-ar -t "${RUCUX_ROOT}/builddir/libc.a" > /dev/null || true # dummy check
cp "${RUCUX_ROOT}/builddir/libc.a" "${SYSROOT}/usr/lib/libc_thin.a"
cd "${RUCUX_ROOT}/builddir"
x86_64-elf-ar -M <<EOF
CREATE ${SYSROOT}/usr/lib/libc.a
ADDLIB libc.a
SAVE
END
EOF
cd "${RUCUX_ROOT}"

echo "=============================================="
echo " RUCUX PORTS SYSTEM INITIALIZED"
echo " Sysroot: ${SYSROOT}"
echo " Compiler: ${CC}"
echo "=============================================="

# Helper function to build a specific port
build_port() {
    local port_name=$1
    local port_dir="${RUCUX_ROOT}/ports/${port_name}"
    
    if [ ! -f "${port_dir}/build.sh" ]; then
        echo "Error: Port '${port_name}' not found or missing build.sh"
        exit 1
    fi
    
    echo ">> Building Port: ${port_name} <<"
    cd "${port_dir}"
    
    # Source the port's build script
    source "${port_dir}/build.sh"
    
    echo ">> Port '${port_name}' built successfully. <<"
    echo "----------------------------------------------"
}

# 2. Build Dependency Tree
# Order matters! zlib has no dependencies.
build_port "openlibm"
build_port "zlib"

# Uncomment as we implement them:
build_port "libressl"
build_port "ncurses"
build_port "curl"
build_port "libtorrent"
# build_port "libtorrent"
# build_port "rtorrent"

echo "All ports built and installed to sysroot successfully!"
