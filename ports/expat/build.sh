#!/bin/bash
set -e

EXPAT_VERSION="2.5.0"
EXPAT_TARBALL="expat-${EXPAT_VERSION}.tar.gz"
EXPAT_URL="https://github.com/libexpat/libexpat/releases/download/R_2_5_0/${EXPAT_TARBALL}"
EXPAT_DIR="${PORTS_BUILD_DIR}/expat-${EXPAT_VERSION}"

if [ ! -f "${PORTS_BUILD_DIR}/${EXPAT_TARBALL}" ]; then
    echo "Downloading ${EXPAT_TARBALL}..."
    curl -sSL -o "${PORTS_BUILD_DIR}/${EXPAT_TARBALL}" "${EXPAT_URL}"
fi

if [ ! -d "${EXPAT_DIR}" ]; then
    echo "Extracting..."
    tar -xzf "${PORTS_BUILD_DIR}/${EXPAT_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${EXPAT_DIR}"

echo "Configuring expat..."

./configure --host=x86_64-elf \
            --prefix="${SYSROOT}/usr" \
            --disable-shared \
            --enable-static \
            --without-docbook \
            --without-tests \
            --without-examples

echo "Compiling expat..."
make -j$(sysctl -n hw.ncpu || nproc) -C lib

echo "Installing expat..."
make install -C lib
