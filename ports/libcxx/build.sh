#!/bin/bash
set -e

LLVM_VERSION="19.1.0"
LLVM_TARBALL="llvm-project-${LLVM_VERSION}.src.tar.xz"
LLVM_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${LLVM_TARBALL}"
LLVM_DIR="${PORTS_BUILD_DIR}/llvm-project-${LLVM_VERSION}.src"

if [ ! -f "${PORTS_BUILD_DIR}/${LLVM_TARBALL}" ]; then
    echo "Downloading ${LLVM_TARBALL}..."
    curl -sSL -L -o "${PORTS_BUILD_DIR}/${LLVM_TARBALL}" "${LLVM_URL}"
fi

if [ ! -d "${LLVM_DIR}" ]; then
    echo "Extracting..."
    tar -xf "${PORTS_BUILD_DIR}/${LLVM_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${LLVM_DIR}"
mkdir -p build
cd build

echo "Configuring libc++ for rucux sysroot..."

# Create a cmake toolchain file
cat > toolchain.cmake <<EOF
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-elf-gcc)
set(CMAKE_CXX_COMPILER x86_64-elf-g++)
set(CMAKE_AR x86_64-elf-ar)
set(CMAKE_RANLIB x86_64-elf-ranlib)

set(CMAKE_C_FLAGS "\${CMAKE_C_FLAGS} ${CFLAGS}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "\${CMAKE_CXX_FLAGS} ${CXXFLAGS}" CACHE STRING "" FORCE)

set(CMAKE_FIND_ROOT_PATH "${SYSROOT}")
set(CMAKE_SYSROOT "${SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF

cmake -G "Unix Makefiles" -Wno-dev \
    -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${SYSROOT}/usr" \
    -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
    -DLIBCXX_ENABLE_SHARED=OFF \
    -DLIBCXX_ENABLE_STATIC=ON \
    -DLIBCXX_ENABLE_EXCEPTIONS=ON \
    -DLIBCXX_ENABLE_RTTI=ON \
    -DLIBCXX_CXX_ABI=libcxxabi \
    -DLIBCXX_ENABLE_THREADS=OFF \
    -DLIBCXX_ENABLE_WIDE_CHARACTERS=OFF \
    -DLIBCXXABI_ENABLE_SHARED=OFF \
    -DLIBCXXABI_ENABLE_STATIC=ON \
    -DLIBCXXABI_ENABLE_THREADS=OFF \
    -DLIBCXXABI_ENABLE_EXCEPTIONS=ON \
    -DLIBCXXABI_ENABLE_RTTI=ON \
    -DLIBUNWIND_ENABLE_SHARED=OFF \
    -DLIBUNWIND_ENABLE_STATIC=ON \
    -DLIBUNWIND_ENABLE_THREADS=OFF \
    -DCMAKE_CXX_COMPILER_WORKS=ON \
    -DCMAKE_C_COMPILER_WORKS=ON \
    ../runtimes

echo "Compiling..."
make -j$(sysctl -n hw.ncpu || nproc) cxx cxxabi unwind

echo "Installing to sysroot..."
make install-cxx install-cxxabi install-unwind

# We need to ensure that the static libraries and headers are in the right place
