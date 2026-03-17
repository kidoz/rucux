#!/bin/bash
set -e

# curl 8.4.0 (The data transfer library for rtorrent)
CURL_VERSION="8.4.0"
CURL_TARBALL="curl-${CURL_VERSION}.tar.gz"
CURL_URL="https://curl.se/download/${CURL_TARBALL}"
CURL_DIR="${PORTS_BUILD_DIR}/curl-${CURL_VERSION}"

# 1. Download
if [ ! -f "${PORTS_BUILD_DIR}/${CURL_TARBALL}" ]; then
    echo "Downloading ${CURL_TARBALL}..."
    curl -sSL -o "${PORTS_BUILD_DIR}/${CURL_TARBALL}" "${CURL_URL}"
fi

# 2. Extract
if [ ! -d "${CURL_DIR}" ]; then
    echo "Extracting..."
    tar -xzf "${PORTS_BUILD_DIR}/${CURL_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${CURL_DIR}"

# 3. Configure for static, freestanding OS
echo "Configuring curl for rucux sysroot..."

# We link against our recently ported LibreSSL and zlib.
export LIBS="-lc"

# We use several cache variables to bypass failing cross-compilation checks.
./configure --host=x86_64-elf \
            --prefix="${SYSROOT}/usr" \
            --disable-shared \
            --enable-static \
            --with-openssl="${SYSROOT}/usr" \
            --with-zlib="${SYSROOT}/usr" \
            --disable-threaded-resolver \
            --disable-unix-sockets \
            --disable-manual \
            --disable-proxy \
            --disable-file \
            --disable-ftp \
            --disable-ldap \
            --disable-rtsp \
            --disable-dict \
            --disable-tftp \
            --disable-pop3 \
            --disable-imap \
            --disable-smb \
            --disable-smtp \
            --disable-gopher \
            --disable-mqtt \
            --without-libpsl \
            --without-libidn2 \
            --without-brotli \
            --without-zstd \
            curl_cv_func_printf_ptr=yes \
            curl_cv_func_recv_test=yes \
            curl_cv_func_send_test=yes \
            curl_cv_func_select_test=yes \
            curl_cv_func_poll_test=yes \
            ac_cv_func_fseeko=yes \
            ac_cv_func_gethostbyname=yes \
            CFLAGS="${CFLAGS} -O2" \
            LDFLAGS="${LDFLAGS}" \
            OPENSSL_LIBS="-lssl -lcrypto -lz" \
            ZLIB_LIBS="-lz"

# 4. Compile
echo "Compiling curl..."
make -j$(sysctl -n hw.ncpu || nproc)

# 5. Install to Sysroot
echo "Installing to sysroot..."
make install
