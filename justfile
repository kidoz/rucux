# Run clang-format on all C/C++ files
format:
    ninja -C build/dev-qemu-amd64 clang-format

# Run clang-tidy on all C/C++ files
lint:
    ninja -C build/dev-qemu-amd64 clang-tidy

# Run C++ quality checks, Python checks, and manifest validation
check: format lint py-check validate

# Validate every product, component policy, and assemblable image
validate:
    python3 tools/validate_manifests.py

# Reconfigure an existing product build directory
reconfigure product="dev-qemu-amd64":
    meson setup --reconfigure build/{{product}}

# Configure and build a product with its architecture's cross file
build product="dev-qemu-amd64":
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{justfile_directory()}}"
    arch=$(python3 tools/product_info.py get "{{product}}" arch)
    case "${arch}" in
        amd64) cross_file=cross_amd64.txt ;;
        armv7) cross_file=cross_armv7.txt ;;
        aarch64) cross_file=cross_aarch64.txt ;;
        *) echo "unsupported product architecture: ${arch}" >&2; exit 1 ;;
    esac
    build_dir="build/{{product}}"
    if [ -d "${build_dir}" ]; then
        meson setup --reconfigure "${build_dir}" --cross-file "${cross_file}" -Dproduct="{{product}}"
    else
        meson setup "${build_dir}" --cross-file "${cross_file}" -Dproduct="{{product}}"
    fi
    meson compile -C "${build_dir}"

# Assemble a declared product image
assemble product="dev-qemu-amd64" image="uefi-disk":
    python3 tools/assemble_image.py {{product}} {{image}} --build-dir build/{{product}}

# Run a product in QEMU
run product="dev-qemu-amd64": (build product)
    scripts/run_qemu.sh --product {{product}}

# Run a product headless with a timeout
run-headless timeout="15" product="dev-qemu-amd64": (build product)
    scripts/run_qemu.sh --product {{product}} --headless --timeout {{timeout}}

# Run all unit tests (host-compiled)
test:
    make -C tests run

# Run all three native SMP gates offline with retained per-architecture evidence
test-smp attempts="5":
    python3 tools/test_native_smp.py --attempts {{quote(attempts)}}

# Install Python tool dependencies into .venv (from uv.lock)
py-sync:
    uv --cache-dir build/python-tools/uv-cache sync --locked --all-groups

# Format project-owned Python scripts
py-format:
    uv --cache-dir build/python-tools/uv-cache run --offline --no-sync ruff format tools scripts

# Lint without changing files or downloading dependencies
py-lint:
    uv --cache-dir build/python-tools/uv-cache run --offline --no-sync ruff check tools scripts

# Type-check all project-owned Python scripts
py-typecheck:
    uv --cache-dir build/python-tools/uv-cache run --offline --no-sync mypy

# Check Python lint, types, and formatting without modifying source files
py-check: py-lint py-typecheck
    uv --cache-dir build/python-tools/uv-cache run --offline --no-sync ruff format --check tools scripts

# Run a project Python tool, e.g. `just py tools/product_info.py get dev-qemu-amd64 name`
py *ARGS:
    uv run python {{ARGS}}

# Generate a concrete release manifest
release-manifest release:
    python3 tools/generate_release_manifest.py {{release}}

# Clean current build artifacts
clean:
    rm -rf build
