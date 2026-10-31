# Storage persistence smoke

This amd64 product runs `storage_smoke` through the public custom-libc API.
It needs local Meson/cross-compiler, QEMU, mtools, UEFI firmware, and the existing
`bootloader/BOOTX64.EFI`; the test does not install or fetch prerequisites.

From the repository root:

```sh
meson setup build/smoke-storage-qemu-amd64 --cross-file cross_amd64.txt \
  -Dproduct=smoke-storage-qemu-amd64
meson compile -C build/smoke-storage-qemu-amd64
python3 tools/test_storage_smoke.py
```

For an existing matching build directory, use `meson setup --reconfigure`.
The tool defaults to Homebrew QEMU firmware paths. Override `--uefi-code`,
`--uefi-vars`, `--bootloader`, or `--build-dir` for another local installation.
`--timeout` bounds each boot and defaults to 30 seconds.

The tool creates a fresh image under `build/smoke-storage-qemu-amd64/storage-smoke/`
on every invocation, replacing that test image and its firmware variables/logs.
It boots the same image three times with one CPU and `-nic none`, requires the
guest PASS marker and successful QEMU poweroff, and extracts files after each
boot to check their exact length and contents independently.

1. Shrink 1024 bytes to 37 without a later write to that file. Truncate another
   preallocated file to zero, rewrite 700 bytes, and fsync. Check descriptor
   offsets, RAMFS shrinking/gap clearing, and libc error translation.
2. Remount and verify both files, then truncate the rewritten file to zero and
   fsync without a later write.
3. Remount, verify zero EOF, and rewrite 700 bytes using the retained FAT chain.

Successful FAT32 shrinking persists directory EOF and keeps all allocated
clusters. Growth via `ftruncate` returns `EOPNOTSUPP`; writes can reuse the
retained capacity. Fsync reports device flush failures. RAMFS remains volatile.
This is clean shutdown/remount evidence under atomic sector and honest flush
assumptions; it does not model torn sectors or a physical power cut. See
[hosted crash tests](../../tests/README.md) for the interruption model.
