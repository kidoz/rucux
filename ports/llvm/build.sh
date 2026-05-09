#!/bin/bash
set -e

LLVM_VERSION="19.1.0"
LLVM_TARBALL="llvm-project-${LLVM_VERSION}.src.tar.xz"
LLVM_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${LLVM_TARBALL}"
LLVM_DIR="${PORTS_BUILD_DIR}/llvm-project-${LLVM_VERSION}.src"

if [ ! -f "${PORTS_BUILD_DIR}/${LLVM_TARBALL}" ]; then
    echo "Downloading ${LLVM_TARBALL}..."
    curl -sSL -o "${PORTS_BUILD_DIR}/${LLVM_TARBALL}" "${LLVM_URL}"
fi

if [ ! -d "${LLVM_DIR}" ]; then
    echo "Extracting..."
    tar -xf "${PORTS_BUILD_DIR}/${LLVM_TARBALL}" -C "${PORTS_BUILD_DIR}"
fi

cd "${LLVM_DIR}"

cat << 'EOF' > patch_llvm.py
import os
import sys

patches = {
    "llvm/include/llvm/Support/ScaledNumber.h": "#define INT32_MIN (-2147483647 - 1)\n",
    "llvm/lib/Support/CrashRecoveryContext.cpp": "#define SIGTRAP 5\n#define EX_IOERR 74\n"
}

base_dir = "."

for file_path, prepend_content in patches.items():
    full_path = os.path.join(base_dir, file_path)
    if os.path.exists(full_path):
        with open(full_path, 'r') as f:
            content = f.read()
        if not content.startswith(prepend_content.split('\n')[0]):
            with open(full_path, 'w') as f:
                f.write(prepend_content + content)

for file in ["llvm/include/llvm/Support/ConvertUTF.h", "llvm/lib/Support/ConvertUTFWrapper.cpp"]:
    full_path = os.path.join(base_dir, file)
    with open(full_path, 'r') as f:
        content = f.read()
    content = content.replace("std::wstring", "std::string")
    content = content.replace("wchar_t *", "char *")
    content = content.replace("const wchar_t *", "const char *")
    with open(full_path, 'w') as f:
        f.write(content)
EOF
python3 patch_llvm.py

mkdir -p build_llvm
cd build_llvm

echo "Configuring LLVM for rucux sysroot..."

# For LLVM, we NEED the libc++ headers, so we keep CXXFLAGS as is.
# We also need to strip freestanding restrictions
LLVM_CXXFLAGS=$(echo "$CXXFLAGS" | sed "s|-fno-exceptions||g" | sed "s|-fno-rtti||g" | sed "s|-ffreestanding||g")
CLEAN_CFLAGS=$(echo "$CFLAGS" | sed "s|-ffreestanding||g")

cat > toolchain.cmake <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-elf-gcc)
set(CMAKE_CXX_COMPILER x86_64-elf-g++)
set(CMAKE_AR x86_64-elf-ar)
set(CMAKE_RANLIB x86_64-elf-ranlib)

set(CMAKE_C_FLAGS "\${CMAKE_C_FLAGS} ${CLEAN_CFLAGS}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "\${CMAKE_CXX_FLAGS} ${LLVM_CXXFLAGS}" CACHE STRING "" FORCE)

set(CMAKE_EXE_LINKER_FLAGS "-nostdlib -L${SYSROOT}/usr/lib" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS "-nostdlib -L${SYSROOT}/usr/lib" CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES "-lc++ -lc++abi -lunwind -lc -lm" CACHE STRING "" FORCE)
set(CMAKE_C_STANDARD_LIBRARIES "-lc -lm" CACHE STRING "" FORCE)

set(CMAKE_FIND_ROOT_PATH "${SYSROOT}")
set(CMAKE_SYSROOT "${SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF

env -u CC -u CXX -u CFLAGS -u CXXFLAGS -u LDFLAGS -u AR -u RANLIB \
cmake -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake \
    -DCMAKE_INSTALL_PREFIX="${SYSROOT}/usr" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_TARGETS_TO_BUILD="X86" \
    -DLLVM_DEFAULT_TARGET_TRIPLE="x86_64-pc-elf" \
    -DLLVM_ENABLE_THREADS=ON \
    -DLLVM_ENABLE_ZLIB=OFF \
    -DLLVM_ENABLE_ZSTD=OFF \
    -DLLVM_ENABLE_TERMINFO=OFF \
    -DLLVM_ENABLE_LIBEDIT=OFF \
    -DLLVM_ENABLE_LIBXML2=OFF \
    -DLLVM_ENABLE_BACKTRACES=OFF \
    -DLLVM_BUILD_TOOLS=OFF \
    -DLLVM_INCLUDE_TOOLS=OFF \
    -DLLVM_INCLUDE_UTILS=OFF \
    -DLLVM_INCLUDE_TESTS=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_INCLUDE_BENCHMARKS=OFF \
    -DLLVM_ENABLE_PIC=OFF \
    -DLLVM_ENABLE_RTTI=ON \
    -DLLVM_ENABLE_EH=ON \
    -DHAVE_POSIX_REGEX=0 \
    -DHAVE_STEADY_CLOCK=0 \
    -DHAVE_CXX_ATOMICS_WITHOUT_LIB=ON \
    -DHAVE_CXX_ATOMICS64_WITHOUT_LIB=ON \
    -DHAVE_GETPAGESIZE=1 \
    -DHAVE_SYSCONF=1 \
    -DHAVE_GETRUSAGE=1 \
    -DHAVE_DLOPEN=1 \
    -DHAVE_FUTIMENS=0 \
    -DHAVE_FUTIMES=0 \
    ../llvm || true

echo "Compiling LLVM..."
env -u CC -u CXX -u CFLAGS -u CXXFLAGS -u LDFLAGS -u AR -u RANLIB ninja

echo "Installing LLVM..."
env -u CC -u CXX -u CFLAGS -u CXXFLAGS -u LDFLAGS -u AR -u RANLIB ninja install
