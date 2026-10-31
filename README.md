# rucux

rucux is a freestanding C++23 microkernel and userspace monorepo targeting amd64, ARMv7, and AArch64. Meson builds the kernel, custom libc, services, and programs selected by a product manifest.

## Repository layout

- `src/arch/` — architecture-specific kernel entry, interrupt, syscall, MMU, and platform code.
- `src/kernel/` and `src/include/` — shared kernel implementations and internal headers.
- `uapi/` — stable kernel/userspace ABI headers.
- `lib/` — freestanding runtime support shared by kernel and userspace.
- `libc/` — custom userspace libc.
- `services/` — long-lived system services and their optional `app.yaml` lifecycle policy.
- `programs/` — user-facing programs, tests, and their optional `app.yaml` lifecycle policy.
- `userspace/` — shared CRT startup code, linker scripts, and Meson userspace construction.
- `boards/` — hardware or VM capabilities and logical artifact names.
- `products/` — board, service, program, port, and image selection.
- `images/` — image assembly and runtime definitions.
- `ports/` — third-party package recipes.
- `bootloader/` — UEFI and baremetal loaders.
- `tools/` and `scripts/` — host-side product tooling and operational commands.
- `tests/` — hosted C++ tests.

Each application has exactly one authoritative `main.cpp` under `services/` or `programs/`. Components managed by `init` can declare dependencies and restart behavior in a neighboring `app.yaml`; omitted policy fields default to disabled/zero and no dependencies.

## Build and run

Install Meson, Ninja, the cross toolchains named in `cross_*.txt`, and `just` for the task wrappers. No dependency fetch is performed by the build itself.

```sh
just build                         # dev-qemu-amd64
just build smoke-qemu-aarch64
just build bringup-qemu-armv7
just run                           # build and run dev-qemu-amd64
just run-headless 15               # headless amd64 run with a timeout
```

The `just build <product>` command reads the product architecture and selects the matching cross file. Direct Meson usage follows the same layout:

```sh
meson setup build/smoke-qemu-amd64 --cross-file cross_amd64.txt -Dproduct=smoke-qemu-amd64
meson compile -C build/smoke-qemu-amd64
```

The development amd64 product also requires its Wayland sysroot and port archives. Use `smoke-qemu-amd64` for a smaller amd64 build without those dependencies.

## Outputs

Generated files stay out of the source directories:

- `build/<product>/` — Meson files, kernel, libc, and userspace binaries.
- `build/images/<product>/` — assembled images and QEMU firmware-variable copies.
- `build/logs/<product>/` — smoke and runtime logs.
- `build/host-tests/` — hosted test objects and executables.
- `build/packages/` and `build/ports/` — port packages and build outputs.
- `build/release/<release>/` — concrete release manifests and artifacts.
- `build/python-tools/` — Python tool caches.

Assemble or release artifacts with:

```sh
just assemble dev-qemu-amd64 uefi-disk
just release-manifest dev-qemu-amd64
```

## Verification

```sh
just test                 # hosted C++ tests
just py-check             # Python lint, types, and formatting
just validate             # all product/component/image manifests
scripts/test_smoke_all.sh # QEMU poweroff/reboot gates; needs target prerequisites
```

See [hosted test and fuzzing commands](tests/README.md) for reproducible stateful
network campaigns and the optional Clang/libFuzzer target.

CI runs host checks, manifest validation, and a three-architecture cross-build matrix. QEMU smoke tests remain separate because they require architecture-specific emulators, firmware, and host tools.
