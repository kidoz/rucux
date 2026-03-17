#!/bin/bash
set -e

# libtorrent (rakshasa) 0.13.8
LIBTORRENT_VERSION="0.13.8"
LIBTORRENT_TARBALL="libtorrent-${LIBTORRENT_VERSION}.tar.gz"
LIBTORRENT_URL="https://github.com/rakshasa/libtorrent/archive/refs/tags/v${LIBTORRENT_VERSION}.tar.gz"
LIBTORRENT_DIR="${PORTS_BUILD_DIR}/libtorrent-${LIBTORRENT_VERSION}"

# 1. Download
if [ ! -f "${PORTS_BUILD_DIR}/${LIBTORRENT_TARBALL}" ]; then
    echo "Downloading ${LIBTORRENT_TARBALL}..."
    curl -sSL -o "${PORTS_BUILD_DIR}/${LIBTORRENT_TARBALL}" "${LIBTORRENT_URL}"
fi

# 2. Extract
if [ ! -d "${LIBTORRENT_DIR}" ]; then
    echo "Extracting..."
    tar -xzf "${PORTS_BUILD_DIR}/${LIBTORRENT_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${LIBTORRENT_DIR}"

if [ ! -f "configure" ]; then
    echo "Running autogen.sh..."
    ./autogen.sh
fi

# 3. Configure
echo "Configuring libtorrent for rucux sysroot..."

export CXXFLAGS="${CFLAGS} -O2 -std=c++14"

# It uses pkg-config, so let's point it to our sysroot
export PKG_CONFIG_PATH="${SYSROOT}/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="${SYSROOT}"

./configure --host=x86_64-elf 
            --prefix="${SYSROOT}/usr" 
            --disable-shared 
            --enable-static 
            --with-zlib="${SYSROOT}/usr" 
            --with-openssl="${SYSROOT}/usr"

# 4. Compile
echo "Compiling libtorrent..."
make -j$(sysctl -n hw.ncpu || nproc)

# 5. Install to Sysroot
echo "Installing to sysroot..."
make install
