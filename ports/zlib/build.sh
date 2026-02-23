#!/bin/bash
set -e

# zlib 1.3.1 (A robust compression library used by everything)
ZLIB_VERSION="1.3.1"
ZLIB_TARBALL="zlib-${ZLIB_VERSION}.tar.gz"
ZLIB_URL="https://zlib.net/fossils/zlib-${ZLIB_VERSION}.tar.gz"
ZLIB_DIR="${PORTS_BUILD_DIR}/zlib-${ZLIB_VERSION}"

# 1. Download
if [ ! -f "${PORTS_BUILD_DIR}/${ZLIB_TARBALL}" ]; then
    echo "Downloading ${ZLIB_TARBALL}..."
    curl -sSL -o "${PORTS_BUILD_DIR}/${ZLIB_TARBALL}" "${ZLIB_URL}"
fi

# 2. Extract
if [ ! -d "${ZLIB_DIR}" ]; then
    echo "Extracting..."
    tar -xzf "${PORTS_BUILD_DIR}/${ZLIB_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${ZLIB_DIR}"

# 3. Configure for static, freestanding OS
echo "Configuring zlib for rucux sysroot..."

# zlib's configure script is simple. We force it to build only static libs.
# We also have to pass --prefix so it knows where to "install" the headers.
# CHOST forces cross-compilation mode in zlib's custom configure script.
CHOST=x86_64-elf ./configure --static --prefix="${SYSROOT}/usr"

# 4. Compile
echo "Compiling..."
# We have to inject our strict freestanding CFLAGS because zlib might assume Linux
make -j$(sysctl -n hw.ncpu || nproc) CFLAGS="${CFLAGS} -O2" LDSHARED="x86_64-elf-gcc" LDFLAGS="${LDFLAGS}" LDLIBS="${SYSROOT}/usr/lib/libc.a" libz.a

# 5. Install to Sysroot
echo "Installing to sysroot..."
cp zlib.h zconf.h "${SYSROOT}/usr/include/"
cp libz.a "${SYSROOT}/usr/lib/"

# Clean up shared library cruft it might have accidentally built despite --static
rm -f "${SYSROOT}/usr/lib/libz.so*"
