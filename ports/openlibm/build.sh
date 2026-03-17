#!/bin/bash
set -e

# OpenLibm 0.8.3 (Standalone Math Library)
OPENLIBM_VERSION="0.8.3"
OPENLIBM_TARBALL="v${OPENLIBM_VERSION}.tar.gz"
OPENLIBM_URL="https://github.com/JuliaMath/openlibm/archive/refs/tags/${OPENLIBM_TARBALL}"
OPENLIBM_DIR="${PORTS_BUILD_DIR}/openlibm-${OPENLIBM_VERSION}"

# 1. Download
if [ ! -f "${PORTS_BUILD_DIR}/${OPENLIBM_TARBALL}" ]; then
    echo "Downloading openlibm ${OPENLIBM_TARBALL}..."
    curl -sSL -o "${PORTS_BUILD_DIR}/${OPENLIBM_TARBALL}" "${OPENLIBM_URL}"
fi

# 2. Extract
if [ ! -d "${OPENLIBM_DIR}" ]; then
    echo "Extracting..."
    tar -xzf "${PORTS_BUILD_DIR}/${OPENLIBM_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${OPENLIBM_DIR}"

# 3. Compile
echo "Compiling openlibm..."
# OpenLibm's Makefile uses OS and ARCH variables for targeting.
# We set OS=Linux just to get generic POSIX/ELF behavior.
make -j$(sysctl -n hw.ncpu || nproc) \
    OS=Linux \
    ARCH=x86_64 \
    CC="x86_64-elf-gcc" \
    AR="x86_64-elf-ar" \
    CPPFLAGS="-I${SYSROOT}/usr/include" \
    CFLAGS="-ffreestanding -O2 -fno-exceptions -fno-rtti -mcmodel=large" \
    libopenlibm.a

# 4. Install to Sysroot
echo "Installing to sysroot..."
mkdir -p "${SYSROOT}/usr/include"
mkdir -p "${SYSROOT}/usr/lib"

# Install headers
cp include/openlibm*.h "${SYSROOT}/usr/include/"
cp src/cdefs-compat.h "${SYSROOT}/usr/include/"
cp src/types-compat.h "${SYSROOT}/usr/include/"

# Install library as libm.a so standard -lm works
cp libopenlibm.a "${SYSROOT}/usr/lib/libm.a"
x86_64-elf-ranlib "${SYSROOT}/usr/lib/libm.a"
