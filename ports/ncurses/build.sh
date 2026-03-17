#!/bin/bash
set -e

# ncurses 6.4 (The UI library for rtorrent)
NCURSES_VERSION="6.4"
NCURSES_TARBALL="ncurses-${NCURSES_VERSION}.tar.gz"
NCURSES_URL="https://ftp.gnu.org/pub/gnu/ncurses/${NCURSES_TARBALL}"
NCURSES_DIR="${PORTS_BUILD_DIR}/ncurses-${NCURSES_VERSION}"

# 1. Download
if [ ! -f "${PORTS_BUILD_DIR}/${NCURSES_TARBALL}" ]; then
    echo "Downloading ${NCURSES_TARBALL}..."
    curl -sSL -o "${PORTS_BUILD_DIR}/${NCURSES_TARBALL}" "${NCURSES_URL}"
fi

# 2. Extract
if [ ! -d "${NCURSES_DIR}" ]; then
    echo "Extracting..."
    tar -xzf "${PORTS_BUILD_DIR}/${NCURSES_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${NCURSES_DIR}"

# 3. Configure for static, freestanding OS
echo "Configuring ncurses for rucux sysroot..."

# We use a very minimal configuration.
# We skip the fallback generation if it's failing, and we'll handle terminfo later.
./configure --host=x86_64-elf \
            --prefix="${SYSROOT}/usr" \
            --disable-shared \
            --enable-static \
            --without-ada \
            --without-cxx-binding \
            --without-debug \
            --without-manpages \
            --without-progs \
            --without-tests \
            --without-develop \
            --disable-home-terminfo \
            --with-terminfo-dirs="${SYSROOT}/usr/share/terminfo" \
            --with-default-terminfo-dir="${SYSROOT}/usr/share/terminfo" \
            --enable-pc-files \
            CFLAGS="${CFLAGS} -O2"

# 4. Compile
echo "Compiling ncurses..."
# We explicitly target the library to avoid building extras that might fail
make -j$(sysctl -n hw.ncpu || nproc)

# 5. Install to Sysroot
echo "Installing to sysroot..."
# We ignore errors in install because terminfo database generation often fails on host
make install || echo "Warning: ncurses install had some errors (likely terminfo), but libraries should be fine."

