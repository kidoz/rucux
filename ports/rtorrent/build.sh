#!/bin/bash
set -e

# rtorrent 0.9.8
RTORRENT_VERSION="0.9.8"
RTORRENT_TARBALL="rtorrent-${RTORRENT_VERSION}.tar.gz"
RTORRENT_URL="https://github.com/rakshasa/rtorrent/archive/refs/tags/v${RTORRENT_VERSION}.tar.gz"
RTORRENT_DIR="${PORTS_BUILD_DIR}/rtorrent-${RTORRENT_VERSION}"

# 1. Download
if [ ! -f "${PORTS_BUILD_DIR}/${RTORRENT_TARBALL}" ]; then
    echo "Downloading ${RTORRENT_TARBALL}..."
    curl -sSL -o "${PORTS_BUILD_DIR}/${RTORRENT_TARBALL}" "${RTORRENT_URL}"
fi

# 2. Extract
if [ ! -d "${RTORRENT_DIR}" ]; then
    echo "Extracting..."
    tar -xzf "${PORTS_BUILD_DIR}/${RTORRENT_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${RTORRENT_DIR}"

if [ ! -f "configure" ]; then
    echo "Running autogen.sh..."
    ./autogen.sh
fi

# 3. Configure
echo "Configuring rtorrent for rucux sysroot..."

export CXXFLAGS="${CXXFLAGS} -I${SYSROOT}/usr/include/ncurses -DHAVE_NCURSES_H -std=c++14 -DNCURSES_ENABLE_STDBOOL_H=0 -include functional"
export CPPFLAGS="${CPPFLAGS} -I${SYSROOT}/usr/include/ncurses -DHAVE_NCURSES_H"
export PKG_CONFIG_PATH="${SYSROOT}/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="${SYSROOT}"

./configure --host=x86_64-elf \
            --prefix="${SYSROOT}/usr" \
            --with-ncurses \
            --with-xmlrpc-c=no \
            --disable-shared \
            --enable-static \
            --with-libcurl="${SYSROOT}/usr"

# 4. Compile
echo "Compiling rtorrent..."
make -j$(sysctl -n hw.ncpu || nproc)

# 5. Install
echo "Installing to sysroot..."
make install
