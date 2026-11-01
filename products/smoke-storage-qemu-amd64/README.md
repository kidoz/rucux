# Storage persistence smoke

This amd64 product runs `storage_smoke` through the public custom-libc API.
It needs local Meson/cross-compiler, QEMU with blkdebug, mtools, UEFI firmware, and the existing
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

The task-owned image includes `mmap.bin` and `mmaperr.bin`. The tool follows the
latter's FAT chain to locate its second page and puts QEMU's blkdebug filter
under the raw disk driver. Reads of that sector and the first data sector of a
dedicated `faultdir` directory receive EIO; image contents are unchanged. A padding
fixture separates boot-file reads from the fault sectors. The exact block graph is saved as `storage-faults.json`.
Host extraction uses the raw image directly and bypasses fault injection.

Every boot also checks `open`/`stat` errno for a missing file and the valid path
`/fat32/faultdir/known.bin` whose directory read fails. Missing entries report
ENOENT, failed reads EIO, and failed stat calls preserve the output buffer.
Path tests check excessive component/path lengths, malformed FAT32 short names,
non-directory traversal and trailing slashes, repeated slashes, and O_DIRECTORY.
Paths remain absolute, with 127-byte components and a 1024-byte total limit
including NUL; FAT32 retains 8.3 names. Relative paths return EINVAL and dot/parent
components EOPNOTSUPP. The serial marker is
`storage_smoke: lookup errno and path validation PASS`.

Each boot repeats stat/open/map/close/unmap 32 times, checking that private
mapping bytes remain readable after closing the file. Four clone probes check
an inherited file after the parent closes it and deliberately leave the child's
descriptors open. Two children exit normally and two receive SIGTERM while
yielding; each probe explicitly unmaps its user stack after termination.
The required marker is `storage_smoke: vnode references and thread cleanup PASS`.
Hosted allocation counters establish exact reclamation; these target checks
exercise real clone/exit/mapping paths and do not measure kernel heap usage.

Every boot checks an eager private file mapping, snapshot-EOF zero filling,
independent FD offsets, private-copy writes and errno for invalid descriptors
and write-only access. It then repeats first/later-page EIO mappings 16 times,
reusing the same fixed span with zeroed anonymous pages after every failure.
The serial marker is `storage_smoke: mmap read-error rollback PASS`.

1. Shrink 1024 bytes to 37 without a later write to that file. Truncate another
   preallocated file to zero, rewrite 700 bytes, and fsync. Check descriptor
   offsets, RAMFS shrinking/gap clearing, and libc error translation. Before the
   rewrite, open that file three times, shrink to 37, seek an old handle to EOF
   and append, then check the new size through another old handle. Check zero
   shrinking and later growth through those handles too, without an intervening
   read/write/fsync on the handle whose metadata is being queried.
   Reset another preallocated file and append three records through two
   O_APPEND handles, including after an explicit seek to zero. Verify writable
   modes survive F_SETFL, read-only modes cannot be upgraded, and RAMFS can
   toggle append behavior without losing its descriptor offset.
   Check read failure through a write-only descriptor, EOF preserving errno,
   and an ordinary write at full allocated capacity returning ENOSPC without
   moving the offset. A write past EOF returns EOPNOTSUPP for the unsupported gap.
2. Remount and verify both files, then truncate the rewritten file to zero and
   fsync without a later write. Verify the append records and add a boot-2 record
   through a newly opened O_APPEND handle.
3. Remount, verify zero EOF, and rewrite 700 bytes using the retained FAT chain.
   Verify the earlier append records and append a boot-3 record. The host checks
   exact append-file contents after every boot.

Successful FAT32 shrinking persists directory EOF and keeps all allocated
clusters. Growth via `ftruncate` returns `EOPNOTSUPP`; writes can reuse the
retained capacity. Fsync reports device flush failures. RAMFS remains volatile.
Stat/fstat and SEEK_END refresh FAT32 EOF; failed refreshes propagate through
libc as -1/errno. Ordinary reads/writes also report signed errors, with successful
prefix counts advancing only by acknowledged bytes. Disk failures for mmap are
injected through QEMU blkdebug; hosted tests account for exact frame/VMA cleanup
and cover allocation and installation failures.
Append positioning and writing share one filesystem reservation;
partial writes remain possible within fixed allocated capacity. This QEMU test
uses sequential operations across separate opens. Hosted tests exercise reentrant
FAT32 contention and concurrent RAMFS appends; target multi-CPU storage and
concurrent metadata queries remain unvalidated.
This is clean shutdown/remount evidence under atomic sector and honest flush
assumptions; it does not model torn sectors or a physical power cut. See
[hosted crash tests](../../tests/README.md) for the interruption model.
