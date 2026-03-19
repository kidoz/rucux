#!/bin/bash
set -e

# LibreSSL (A cleaner, easier-to-cross-compile fork of OpenSSL)
LIBRESSL_VERSION="3.8.2"
LIBRESSL_TARBALL="libressl-${LIBRESSL_VERSION}.tar.gz"
LIBRESSL_URL="https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/${LIBRESSL_TARBALL}"
LIBRESSL_DIR="${PORTS_BUILD_DIR}/libressl-${LIBRESSL_VERSION}"

# 1. Download
if [ ! -f "${PORTS_BUILD_DIR}/${LIBRESSL_TARBALL}" ]; then
    echo "Downloading ${LIBRESSL_TARBALL}..."
    curl -sSL -o "${PORTS_BUILD_DIR}/${LIBRESSL_TARBALL}" "${LIBRESSL_URL}"
fi

# 2. Extract
if [ ! -d "${LIBRESSL_DIR}" ]; then
    echo "Extracting..."
    tar -xzf "${PORTS_BUILD_DIR}/${LIBRESSL_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${LIBRESSL_DIR}"

# 3. Configure for static, freestanding OS
echo "Configuring LibreSSL for rucux sysroot..."

# We disable shared libraries, tests, and apps. We only want libcrypto.a and libssl.a
# We set host to x86_64-elf so it uses our cross-compiler.
# We add -DOPENSSL_NO_SPEED and -DOPENSSL_NO_ASYNC to disable features that require deep OS integration (like setjmp/longjmp or ucontext)
ac_cv_func_arc4random_buf=yes ac_cv_func_arc4random=yes \
./configure --host=x86_64-elf \
            --prefix="${SYSROOT}/usr" \
            --disable-shared \
            --enable-static \
            CFLAGS="${CFLAGS} -O2 -DOPENSSL_NO_POSIX_IO -DOPENSSL_NO_ASYNC -DHAVE_ARC4RANDOM -DHAVE_ARC4RANDOM_BUF"

sed -i '' 's/^SUBDIRS = .*/SUBDIRS = include crypto ssl tls/' Makefile || sed -i 's/^SUBDIRS = .*/SUBDIRS = include crypto ssl tls/' Makefile

# 4. Compile
echo "Compiling LibreSSL..."
make -j$(sysctl -n hw.ncpu || nproc)

# 5. Install to Sysroot
echo "Installing to sysroot..."
make install
