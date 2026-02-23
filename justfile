# Run clang-format on all C/C++ files
format:
    ninja -C builddir clang-format

# Run clang-tidy on all C/C++ files
lint:
    ninja -C builddir clang-tidy

# Run both formatter and linter
check: format lint

# Reconfigure the build directory
reconfigure:
    meson setup --reconfigure builddir
