#!/bin/bash
set -e

LIBFFI_VERSION="3.4.4"
LIBFFI_TARBALL="libffi-${LIBFFI_VERSION}.tar.gz"
LIBFFI_URL="https://github.com/libffi/libffi/releases/download/v${LIBFFI_VERSION}/${LIBFFI_TARBALL}"
LIBFFI_DIR="${PORTS_BUILD_DIR}/libffi-${LIBFFI_VERSION}"

if [ ! -f "${PORTS_BUILD_DIR}/${LIBFFI_TARBALL}" ]; then
    echo "Downloading ${LIBFFI_TARBALL}..."
    curl -sSL -o "${PORTS_BUILD_DIR}/${LIBFFI_TARBALL}" "${LIBFFI_URL}"
fi

if [ ! -d "${LIBFFI_DIR}" ]; then
    echo "Extracting..."
    tar -xzf "${PORTS_BUILD_DIR}/${LIBFFI_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${LIBFFI_DIR}"

echo "Configuring libffi..."

./configure --host=x86_64-elf \
            --prefix="${SYSROOT}/usr" \
            --disable-shared \
            --enable-static \
            --disable-docs

echo "Compiling libffi..."
make -j$(sysctl -n hw.ncpu || nproc)

echo "Installing libffi..."
make install
