#!/bin/bash
set -e

# Rucux Ports System - Master Builder
# Cross-compiles 3rd party software targeting the rucux microkernel sysroot

# 1. Setup Environment
export RUCUX_ROOT=$(cd "$(dirname "$0")/.." && pwd)
export SYSROOT="${RUCUX_ROOT}/sysroot"

# Toolchain (AMD64 by default for now)
export RUCUX_ARCH="amd64"
export CC="x86_64-elf-gcc"
export CXX="x86_64-elf-g++"
export AR="x86_64-elf-ar"
export RANLIB="x86_64-elf-ranlib"
export LD="x86_64-elf-ld"
export CPP="x86_64-elf-gcc -E"
export CXXCPP="x86_64-elf-g++ -E"

# Flags to force compilation against our custom sysroot and freestanding environment
export CFLAGS="-ffreestanding -mcmodel=large -fno-stack-protector -isystem ${SYSROOT}/usr/include -isystem ${RUCUX_ROOT}/libc/include -isystem ${RUCUX_ROOT}/lib/include -isystem ${RUCUX_ROOT}/src/include"
export CPPFLAGS="${CFLAGS}"
export CXXFLAGS="-ffreestanding -mcmodel=large -fno-stack-protector -I${SYSROOT}/usr/include/c++/v1 -isystem ${SYSROOT}/usr/include -isystem ${RUCUX_ROOT}/libc/include -isystem ${RUCUX_ROOT}/lib/include -isystem ${RUCUX_ROOT}/src/include -fexceptions -frtti -std=c++23 -D_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE=1 -D__decay=_V1_decay"
export LDFLAGS="-nostdlib -static -L${SYSROOT}/usr/lib"
export LIBS="-Wl,--start-group ${SYSROOT}/usr/lib/libc++.a ${SYSROOT}/usr/lib/libc++abi.a ${SYSROOT}/usr/lib/libunwind.a ${SYSROOT}/usr/lib/libc.a -Wl,--end-group"

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
if [ -z "$1" ]; then
    echo "Error: Please specify a product name (e.g., dev-qemu-amd64)"
    echo "Usage: $0 <product_name>"
    exit 1
fi

PRODUCT_NAME=$1
echo "Resolving ports for product: ${PRODUCT_NAME}..."

PORTS_TO_BUILD=$(${RUCUX_ROOT}/tools/resolve_ports.py "${PRODUCT_NAME}")

if [ $? -ne 0 ]; then
    echo "Error: Failed to resolve ports for ${PRODUCT_NAME}"
    exit 1
fi

echo "Build order:"
echo "${PORTS_TO_BUILD}"
echo "----------------------------------------------"

for port in ${PORTS_TO_BUILD}; do
    build_port "${port}"
done

echo "All ports for ${PRODUCT_NAME} built and installed to sysroot successfully!"
