#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

VULKAN_HEADERS_VERSION="1.3.280"
VULKAN_HEADERS_TARBALL="Vulkan-Headers-v${VULKAN_HEADERS_VERSION}.tar.gz"
VULKAN_HEADERS_URL="https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v${VULKAN_HEADERS_VERSION}.tar.gz"
VULKAN_HEADERS_DIR="${PORTS_BUILD_DIR}/Vulkan-Headers-${VULKAN_HEADERS_VERSION}"

if [ ! -f "${PORTS_BUILD_DIR}/${VULKAN_HEADERS_TARBALL}" ]; then
    echo "Downloading ${VULKAN_HEADERS_TARBALL}..."
    curl -sSL -o "${PORTS_BUILD_DIR}/${VULKAN_HEADERS_TARBALL}" "${VULKAN_HEADERS_URL}"
fi

if [ ! -d "${VULKAN_HEADERS_DIR}" ]; then
    echo "Extracting..."
    tar -xzf "${PORTS_BUILD_DIR}/${VULKAN_HEADERS_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${VULKAN_HEADERS_DIR}"

echo "Installing Vulkan headers..."
mkdir -p "${SYSROOT}/usr/include"
cp -R include/vulkan "${SYSROOT}/usr/include/"

mkdir -p "${SYSROOT}/usr/share/vulkan/registry"
cp registry/vk.xml "${SYSROOT}/usr/share/vulkan/registry/"
