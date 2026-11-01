# Hosted kernel checks

Build and run the normal regression suites from the repository root:

```sh
make -C tests run
```

The network Makefile force-includes `network_test_alloc.hpp` only in production
network translation units. Their new/delete symbols use a tracked allocator;
the host standard library, test framework and fuzzer keep their own allocators.
Keep this boundary when adding build modes so allocation counts remain meaningful.

Run the normal network suite with ASan/UBSan in a separate object directory:

```sh
make -C tests OBJDIR=../build/host-tests/sanitized \
  CXXFLAGS='-std=c++23 -Wall -Wextra -g -fsanitize=address,undefined' \
  LDLIBS='-lpthread -fsanitize=address,undefined' test_network
build/host-tests/sanitized/test_network
```

The RCU suite links production RCU with hosted CPU identity and allocation.
It checks nested/delayed readers, callback epochs, concurrent updaters, callback
reentrancy, synchronous UP progress, and allocation failure. Hosted IRQ helpers
are no-ops; these tests do not prove privileged masking or target memory ordering.

## Stateful network campaigns

The normal network suite includes 32 deterministic cases of 128 operations.
Run a larger campaign or replay a reported seed with:

```sh
make -C tests test_network
build/host-tests/test_network --campaign 12345 256 512
build/host-tests/test_network --campaign 12345 1 512
```

Arguments are the initial unsigned 32-bit seed, number of cases, and operations
per case. Cases and operations are each bounded to 1..1024. Failure output names
the seed and zero-based step and prints a replay command. Case seeds increment
with unsigned 32-bit wraparound.

Each case starts with a fresh established TCP fixture and a bound UDP receiver.
Generated operations cover sends, partial/full ACKs, zero/reopened windows,
timeouts, inbound data/reordering, partial reads, malformed IPv4/UDP packets,
remote FIN, and connection replacement. Oracles check byte order, queue bounds,
sequence progress, and allocation count/bytes returning to baseline. Fixtures
replace allocation, scheduler and packet-output edges; no host sockets are opened.
The fixture starts after the TCP handshake; existing named tests cover handshakes.

## Coverage-guided network fuzzing

Use a Clang installation that includes libFuzzer and the ASan/UBSan runtimes.
Apple Command Line Tools may lack libFuzzer; an existing Homebrew LLVM installation
can be selected explicitly without changing the normal compiler:

```sh
make -C tests fuzz-network CXX=/opt/homebrew/opt/llvm/bin/clang++
mkdir -p build/host-tests/network-corpus build/host-tests/network-artifacts
build/host-tests/fuzz_network build/host-tests/network-corpus \
  -runs=10000 -max_len=3072 \
  -artifact_prefix=build/host-tests/network-artifacts/
```

The binary exports `LLVMFuzzerTestOneInput`; it executes the same stateful
campaign and resets/cleans up fixtures after each input. Three bytes encode an
operation: opcode modulo 12, size/variant, and data/timing value. Trailing partial
operations are ignored, and execution is capped at 1024 operations. Crashing
inputs are saved by libFuzzer and can be replayed by passing their file paths.
Corpus files and failure artifacts belong under `build/`, not in the source tree.

These bounded runs are regression and bug-finding evidence, not exhaustive
verification, snapshot-based execution, NIC/DMA testing, or SMP socket validation.

## FAT32 crash consistency and ATA completion

```sh
make -C tests test_fat32 test_ata test_vfs test_file_sync test_file_metadata test_file_write
build/host-tests/test_fat32
build/host-tests/test_ata
build/host-tests/test_vfs
build/host-tests/test_file_sync
build/host-tests/test_file_metadata
build/host-tests/test_file_write
build/host-tests/test_fat32 --crash 1 1 1 before
build/host-tests/test_fat32 --sync-crash 1 1 0 before
build/host-tests/test_fat32 --append-crash 1 1 0 after
```

The FAT32 suite links the production filesystem to an in-memory ATA adapter.
Its small synthetic volume has two FAT copies, a fragmented two-cluster file,
and a neighboring file in the same directory sector. The generated campaign
enumerates 39 workloads of one to three writes and 534 interruptions: every
sector-write attempt is interrupted both before and after persisting its sector.
After interruption the backend goes offline, volatile mount allocations are
discarded, and the durable image is remounted.

The independent oracle checks that each data sector matches either its pre-write
or post-write contents, acknowledged bytes are visible, newly exposed EOF bytes
are initialized, and unrelated sectors, directory entries and both FAT copies
are unchanged. Normal executions must acknowledge the full supported extent.
Named tests also cover transient data/metadata errors, overflow, capacity limits,
malformed geometry/chains, stale handles, reentrant I/O and partial allocation
cleanup. The test arena simulates losing volatile memory; it does not establish
an unmount/close reclamation API for the VFS.

Failure output prints a replay command. `CODE` encodes operations in base 3,
least significant digit first: overwrite at offset 32, append 700 bytes, or write
900 bytes at the lesser of EOF and offset 500. `DEPTH` is 1..3; `CUT` is the
zero-based sector-write attempt. `before`/`after` controls persistence of that
interrupted sector. A cut beyond the workload's writes executes normally;
`SIZE_MAX` is also accepted to replay an uninterrupted baseline.

The mixed campaign adds 258 workloads and 2656 interruptions over one to three
write/truncate/fsync operations. `--sync-crash` uses base 6: its first three
operations are the writes above (clamping overwrite offsets to EOF after a
shrink), followed by truncate-to-zero, truncate-to-half, and fsync. `CUT` counts
sector writes and explicit flush attempts. A flush does
not change the modeled image; the underlying write adapter already represents
ordered durable sector writes. Truncation may change only the directory EOF,
and each remount must expose the expected bytes without changing the FAT chain.

The append campaign adds 258 workloads and 2854 interruptions through production
VFS with two separately opened O_APPEND handles. `--append-crash` also uses
base 6: append 64 bytes through the first handle, append 700 through the second,
write 900 at the lesser of EOF and offset 500, truncate to zero, truncate to half,
and fsync. The same before/after-sector and EOF oracles apply; descriptor offsets
must move to the acknowledged append end on success and remain unchanged on
failure. Failed writes can still persist unacknowledged data. Named tests cover
stale offsets, partial appends, no space and reentrant append contention.

The VFS suite links production VFS/RAMFS and checks callback dispatch, errors,
access/type validation, unchanged descriptor offsets, and cleared gaps after
shrinking RAMFS. Metadata checks cover refreshed stat/fstat/SEEK_END results,
unchanged stat output/offsets on errors, local files without a refresh callback,
and full-width seek/overflow boundaries. The FAT32 suite also links production
VFS: independent opens observe shrinking, appending, and zero EOF without an
intervening read/write/fsync on the stale handle. Failed reads, changed disk
entries and reservation contention must preserve cached size and caller output.

`test_file_sync` and `test_file_metadata` compile the actual custom-libc wrappers
with only raw syscalls and errno storage replaced. They check argument forwarding,
full-width seek results and -1/errno translation, rather than calling host libc.
`test_file_write` links the real write/fcntl wrappers and checks short results,
storage errors, F_GETFL without a variadic argument, and F_SETFL int forwarding.
It also links the real read wrapper, checking output-buffer forwarding, EOF and
partial results without clearing errno, and -1/errno translation on failures.
VFS tests reject writes through read-only descriptors and verify that F_SETFL
cannot change access modes. Reads through write-only descriptors are rejected.
Signed callback counts retain their full width; impossible byte counts and
offset overflow cannot advance the descriptor. Socket-created descriptors
remain duplex. Device/socket adapters now use signed callbacks too.

FAT32 ordinary I/O tests inject a failed first-sector read (EIO rather than EOF),
partial reads, data/metadata write failures, and reservation contention. They
check untouched output beyond a read prefix, unchanged offsets on failure,
prefix-only offset advances, true EOF zero, and ENOSPC at full capacity.
The existing crash campaigns normalize negative results to zero acknowledged
bytes only after verifying the expected errno; their persistence oracles and
workload/interruption bounds are unchanged.

RAMFS uses a per-file nonwaiting reservation around read/write/truncate/append.
Its hosted concurrent-append test checks all 128 records from four writers for
intact contents and uniqueness, mixing ordinary reads/writes and accepting
EBUSY when another operation owns the reservation. Run the VFS suite under
ThreadSanitizer with:

```sh
make -C tests OBJDIR=../build/host-tests/append-tsan \
  CXXFLAGS='-std=c++23 -Wall -Wextra -g -fsanitize=thread' \
  LDLIBS='-lpthread -fsanitize=thread' test_vfs
build/host-tests/append-tsan/test_vfs
```

The ATA suite links `src/arch/amd64/ata.cpp` against the simulated port interface
in `tests/support/arch/amd64/io.hpp`. It checks transfer/flush errors, device
faults, polling bounds, LBA28 request validation, IDENTIFY and command reservation.
Run these suites under ASan/UBSan with:

```sh
make -C tests OBJDIR=../build/host-tests/fat32-sanitized \
  CXXFLAGS='-std=c++23 -Wall -Wextra -g -fsanitize=address,undefined' \
  LDLIBS='-lpthread -fsanitize=address,undefined' \
  test_fat32 test_ata test_vfs test_file_sync test_file_metadata test_file_write
build/host-tests/fat32-sanitized/test_fat32
build/host-tests/fat32-sanitized/test_ata
build/host-tests/fat32-sanitized/test_vfs
build/host-tests/fat32-sanitized/test_file_sync
build/host-tests/fat32-sanitized/test_file_metadata
build/host-tests/fat32-sanitized/test_file_write
```

These tests assume ordered, atomic 512-byte sectors and a device honoring flush
completion. Failed writes may persist unacknowledged bytes. There is no promise
of whole-write atomicity, torn-sector recovery, reordered unflushed writes,
cluster allocation, sparse writes, or full POSIX filesystem semantics.
FAT32 truncation now durably shrinks EOF while retaining its allocated chain;
truncate growth returns `EOPNOTSUPP`. Fsync explicitly checks ATA flush completion.
RAMFS supports logical shrinking and volatile-memory fsync. FAT32 contention
returns `EBUSY` for lookup/read/write/append/truncate/fsync/metadata refresh;
operations do not wait. Stat/fstat and SEEK_END refresh EOF before using it;
seeking from start/current position needs no metadata I/O.
Refresh failures preserve stat buffers and descriptor offsets. O_APPEND selects
EOF and writes under one filesystem reservation, independent of the FD offset.
Full allocated FAT32 capacity returns `ENOSPC`; an append can acknowledge a
short prefix. Missing regular-file append support returns `EOPNOTSUPP`.
RAMFS remains volatile; busy read/write/append callbacks return `EBUSY` and
allocation failure returns `ENOMEM`. Hosted reservations do not prove target SMP
storage, concurrent metadata queries, or physical power-loss behavior. Whole
writes are not crash-atomic. Checked lookup preserves negative errors through
open/stat and libc errno, as described below. File-backed mmap error handling
is also covered below; stream readiness semantics remain a separate concern.

For real libc/kernel and clean shutdown/remount coverage, see the three-boot
[storage smoke product](../products/smoke-storage-qemu-amd64/README.md).

## Checked pathname lookup

`vfs_ops::lookup` returns zero with a node or negative errno with a null result.
The VFS validates absolute paths before traversal: at most 1023 bytes plus NUL
and 127 bytes per component. Repeated slashes are accepted. A regular file in
an intermediate component or followed by a slash returns `ENOTDIR`;
`O_DIRECTORY` requires a directory. Relative paths return `EINVAL`, and dot and
parent components explicitly return `EOPNOTSUPP`. No symlink/creation semantics
are added. FAT32 retains case-insensitive 8.3 lookup: excess base/extension length
returns `ENAMETOOLONG`, malformed short names return `EINVAL`.

`test_vfs` checks path boundaries, prevalidation before callbacks, missing lookup
operations, malformed callback results, unchanged stat buffers, and descriptor
allocation/exhaustion. Failed opens invoke no open callback and publish no FD.
`test_fat32` checks absence versus read/FAT-chain/corruption/allocation/contention
errors, plus cleanup of partial node allocations. `test_file_metadata` links the
actual libc open wrapper and verifies syscall arguments, FD zero, successful
errno preservation, and -1/errno translation. Run these focused suites with:

```sh
make -C tests test_fat32 test_vfs test_file_metadata
build/host-tests/test_fat32
build/host-tests/test_vfs
build/host-tests/test_file_metadata
```

The storage QEMU smoke creates `faultdir/known.bin` and injects EIO into that
directory's first data sector. Every boot requires open/stat to return EIO for
that valid child path and ENOENT for healthy-directory missing entries, with
unchanged stat output on errors. It also checks component/path limits,
non-directory traversal, repeated slashes, directory-only opens, and libc errno.
The exact block graph is `storage-faults.json`; the serial marker is
`storage_smoke: lookup errno and path validation PASS`.

RAMFS namespace mutation and FD tables have no new concurrency guarantee.
Successful lookup and path resolution return one owned reference. Callers release
it or transfer it to a `node_ref`; descriptor allocation retains a separate
reference. `get_root`, `get_fd_node` and readdir entries are borrowed. `set_root`
and RAMFS attachment consume a reference for the namespace. Permanent RAMFS and
static nodes need no finalizer; managed FAT32, network socket and epoll nodes
finalize after their last reference. Per-descriptor open/close hooks remain
separate from final destruction, including legacy local socket hooks.

FAT32 node cleanup frees its owned name, operations and private data. Shared
mount geometry outlives the root when a child still references it. The mount's
owned name remains distinct from a replacement mount-point label. Traversal
releases intermediate nodes; stat, spawn and failed opens drop temporary pins.
Clone descriptor copies retain nodes; normal and fatal thread exit close their
entries before reaping. Closing detaches the descriptor before invoking callbacks.

`test_fat32` counts live allocations over repeated stat/open/close, nested lookup
and refresh failures, partial allocation failures, FD growth failure, inherited
handles, and mount-root release. Usage returns to the mount baseline, or zero
once all mount and child references are released. `test_vfs` verifies concurrent
retain/release finalizes once, descriptor copying and cleanup, allocation failure,
reentrant close, epoll inheritance and a managed child pinned by RAMFS.
`test_network` checks that retaining a socket node keeps its transport alive
after closing the original descriptor. ASan/UBSan runs should include these
suites plus `test_mmap` and `test_vma`; run VFS and mmap under ThreadSanitizer too.

The storage smoke repeats stat/open/map/close/unmap 32 times each boot and runs
four clone probes. Children use an inherited file after the parent closes its
handle, leave descriptors open, and exit normally or receive SIGTERM while
yielding. Each clone's user stack is explicitly unmapped after termination.
The required serial marker is
`storage_smoke: vnode references and thread cleanup PASS`.

This does not add unmount/unlink, namespace mutation locking, shared open-file
descriptions/offsets, or POSIX dup. Clone still copies descriptor offsets and
flags into separate tables. Fatal delivery now follows the cooperative kernel
cleanup contract below.

## Safe fatal syscall cancellation

`checked_syscall` protects marshalling and backend work with a nested per-thread
kernel scope. The scheduler cannot consume a fatal signal while that scope is
active. Once buffers, filesystem reservations and temporary references unwind,
the outer boundary consumes the signal, closes descriptors, notifies the parent
and schedules away permanently. The amd64/ARMv7 signal-return checks use the same
stop path, preventing a terminated thread from returning to userspace.

Signal bits, masks and thread state use 32-bit atomics. Signal lookup/posting
holds the thread-list lock so the target cannot be reaped mid-post. Interruptible
wait enrollment and posting share a per-thread lock: publish BLOCKED before
releasing the subsystem queue lock, enqueue once when an event races a signal,
then unlink the wait record before clearing enrollment and unwinding.

Supported cancellation points include futex, terminal read, UDP receive, TCP
accept/connect/receive, sleep, waitpid, IRQ wait and poll/select/epoll. Futex
removes its stack waiter even when cancellation won the wake race. Sleep and IRQ
queues retain the target under their queue lock until wakeup completes. mmap
checks fatal requests between pages and rolls back partial population. Bounded
FAT32 operations finish and release their reservation before fatal delivery.
Completed writes and completed mappings retain their normal effects.

`test_cancellation` links production signal policy, syscall marshalling, user
copies and futex code. Scheduler edges record enqueue/termination instead of
switching contexts. An allocation counter checks that marshal buffers are gone
before descriptor cleanup. Tests cover nested scopes, posting during copies,
validation errors, masked TERM/unmaskable KILL, one-time delivery, futex cleanup,
signal/event races, uninterruptible deferral and concurrent signal posts.
The FAT32 and mmap suites inject real signals inside production I/O/population;
subsequent operations must work and allocations/frames return to baseline.
Terminal/network/poll/sleep suites check cancellation cleanup separately.

```sh
make -C tests test_cancellation test_fat32 test_mmap test_network test_tty test_vfs test_time
build/host-tests/test_cancellation

make -C tests OBJDIR=../build/host-tests/cancellation-sanitized \
  CXXFLAGS='-std=c++23 -Wall -Wextra -g -fsanitize=address,undefined' \
  LDLIBS='-lpthread -fsanitize=address,undefined' \
  test_cancellation test_fat32 test_mmap test_vfs test_network test_tty test_time test_user_access

make -C tests OBJDIR=../build/host-tests/cancellation-tsan \
  CXXFLAGS='-std=c++23 -Wall -Wextra -g -fsanitize=thread' \
  LDLIBS='-lpthread -fsanitize=thread' test_cancellation test_mmap test_vfs
```

Run the resulting executables in the chosen object directory. Hosted IRQ masking
is a no-op; host tests do not prove target scheduling or general SMP reclamation.
The storage QEMU smoke confirms each of seven workers is BLOCKED using SYS_TOP,
then sends TERM/KILL during sleep, futex, tty read, UDP receive, TCP accept, poll
or epoll. The worker must terminate without returning from the interrupted
syscall; later futex wake finds no stale waiter, and subsequent storage reads
succeed. Each clone stack is unmapped after termination. The harness requires
`storage_smoke: safe fatal syscall cancellation PASS` on all three boots.

Legacy synchronous/fast IPC waits remain uninterruptible until normal protocol
completion: fatal signals stay pending so peer links and kernel buffers cannot
be abandoned. Ordinary nonfatal signals do not cancel registered waits; full
POSIX EINTR/restart/handler semantics are not added. This is cooperative fatal
cancellation, not arbitrary stack unwinding, rollback of completed side effects,
or a guarantee that an unresponsive device/IPC peer can be killed immediately.
IRQ/waitpid cancellation is cross-built; the seven-operation target campaign
does not exercise those two cancellation paths or TCP connect/receive.

## File-backed mmap population and rollback

```sh
make -C tests test_mmap test_vma test_file_mmap
build/host-tests/test_mmap
build/host-tests/test_vma
build/host-tests/test_file_mmap
```

`test_mmap` links production mmap, VMA and red-black-tree code. Only scheduler,
physical allocation and page-table edges are replaced. The tracked allocator
rejects freeing live mappings, unowned frames or the same frame twice. Tests
check errors on first/later reads, partial-read retries, unexpected early EOF,
zero-filled snapshot-EOF tails, frame/map OOM, borrowed device frames and retries
after failure. Failed calls remove their VMA and installed leaves, release owned
frames and restore the allocation hint when no other mapping advanced it.

Regular-file mappings retain their node during eager population, including a
reentrant descriptor close, then release it once independent copied frames are
ready. Borrowed-frame mappings keep a backing-node reference in each VMA piece;
split/remove/protect, rollback and final address-space cleanup balance those
references. Hosted tests track the finalizer, physical frames and translations,
including partial unmap after close and failed population. Inactive page-table
walks distinguish private frames (including ELF/stack holes) from borrowed VMA
frames and retain VMA metadata until the walk finishes. Kernel/idle switches
restore the kernel page-table root so a dead user root can become inactive.
This is ownership coverage; it does not establish device/GUI runtime or remote
TLB reclamation on SMP targets.

Pending VMAs exclude overlapping mmap/unmap/protect while population or rollback
runs. Reentrant and sibling-thread tests exercise this boundary; another test
replaces a range during an older unmap to check late pending-state validation.
File-backed faults cannot allocate anonymous zero pages. Anonymous mappings
remain eagerly allocated for clone-stack validation and have a zero-fill fault
fallback after population.

`test_vma` runs the existing interval/split/protection regressions. The actual
libc mmap wrapper is exercised by `test_file_mmap`, including errno, six syscall
arguments and sign-extended ARMv7 addresses outside the reserved error range.

```sh
make -C tests OBJDIR=../build/host-tests/mmap-sanitized \
  CXXFLAGS='-std=c++23 -Wall -Wextra -g -fsanitize=address,undefined' \
  LDLIBS='-lpthread -fsanitize=address,undefined' test_mmap test_vma test_file_mmap
build/host-tests/mmap-sanitized/test_mmap
build/host-tests/mmap-sanitized/test_vma
build/host-tests/mmap-sanitized/test_file_mmap

make -C tests OBJDIR=../build/host-tests/mmap-tsan \
  CXXFLAGS='-std=c++23 -Wall -Wextra -g -fsanitize=thread' \
  LDLIBS='-lpthread -fsanitize=thread' test_mmap
build/host-tests/mmap-tsan/test_mmap
```

The storage smoke injects actual QEMU block read errors for the second page of
`mmaperr.bin`, with no kernel test hook. Each of three boots checks first/later
page failures 16 times and reuses the same fixed address with anonymous mappings.
The hosted tests provide exact frame/VMA accounting; the QEMU test exercises
real ATA, FAT32, mappings and libc errno. Hosted MMU stubs do not prove hardware
TLB invalidation or remote CPU reclamation. This eager profile still zero-fills
beyond snapshot EOF, rejects occupied MAP_FIXED ranges, and provides no regular
file shared writeback or SIGBUS-on-EOF implementation.
