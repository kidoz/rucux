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
accept/connect/receive, sleep, waitpid, IRQ wait, poll/select/epoll and the IPC
send/receive/call/wait paths described below. Futex
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
The storage QEMU smoke confirms each of nine workers is BLOCKED using SYS_TOP,
then sends TERM/KILL during sleep, futex, tty read, UDP receive, TCP accept, poll
or epoll, legacy IPC receive or fixed-message IPC wait. The worker must terminate without returning from the interrupted
syscall; later futex wake finds no stale waiter, and subsequent storage reads
succeed. Each clone stack is unmapped after termination. The harness requires
`storage_smoke: safe fatal syscall cancellation PASS` on all three boots.

Ordinary nonfatal signals do not cancel registered waits; full
POSIX EINTR/restart/handler semantics are not added. This is cooperative fatal
cancellation, not arbitrary stack unwinding, rollback of completed side effects,
or a guarantee that an unresponsive device can be killed immediately.
IRQ/waitpid cancellation is cross-built; the nine-operation target campaign
does not exercise those two cancellation paths or TCP connect/receive.

## IPC cancellation and peer teardown

`test_ipc` links production IPC, signal policy, credential checks and shared
syscall marshalling. Host threads and a condition variable replace the scheduler
context-switch edges. Twenty tests cover FIFO legacy sends and fixed calls,
authenticated legacy sender IDs, typed receiver wakeups, the 15-entry async
ring, the 64-request incoming queue limit, queued/accepted cancellation, peer
exit, mutual call cycles, reply/cancellation races and balanced lifetime pins.
Two cases actually free a heap peer/caller before the surviving side resumes
or replies, so ASan can detect an escaped thread pointer. `test_user_access`
checks the new fixed wait's exact 32-byte copyout, unchanged output on backend
failure, and mapping revalidation after a blocked operation.

All peer links are serialized by the IPC lock. An outgoing request owns a peer
pin; its queue or accepted-call slot owns a sender pin. Lookup pins under the
thread-list lock, and completion unlinks and wakes before releasing either
pin. Normal/fatal exit detaches both directions before publishing TERMINATED.
Reaping defers while pins remain; waitpid can consume status without freeing a
pinned thread. No IPC path retains a syscall-buffer or caller-stack pointer.

The public layouts are in `uapi/kernel/ipc.h`: a 40-byte legacy message and
32-byte fixed payload. Existing syscall IDs are preserved; `SYS_IPC_WAIT=64`
exposes the previously unwired fixed-message receive helper through all three
dispatchers and shared output marshalling. IPC uses normal scheduler wakeups;
the former direct context-switch shortcut has been removed. This change makes
no latency/throughput parity claim.

Queued senders/callers receive `-EPIPE` when the peer exits. Cancellation
unlinks a queued request; cancellation after acceptance leaves a tombstone in
the server. `WAIT` returns `-ECANCELED` without consuming it; `REPLY` acknowledges
it with `-ECANCELED` before another call can be accepted. A late old reply thus
cannot satisfy a new caller. The interrupted syscall unwinds with `-EINTR`
internally, then fatal delivery prevents return to userspace. Queue/ring
overflow returns `-EAGAIN`, a missing peer `-ESRCH`, self-call/send `-EDEADLK`,
and waiting with an outstanding accepted call `-EBUSY`. The incoming bound is
64 queued requests across both kinds plus one accepted call; FIFO is per kind.

```sh
make -C tests test_ipc test_user_access
build/host-tests/test_ipc
build/host-tests/test_user_access

make -C tests OBJDIR=../build/host-tests/ipc-sanitized \
  CXXFLAGS='-std=c++23 -Wall -Wextra -g -fsanitize=address,undefined' \
  LDLIBS='-lpthread -fsanitize=address,undefined' \
  test_ipc test_cancellation test_fat32 test_mmap test_user_access

make -C tests OBJDIR=../build/host-tests/ipc-tsan \
  CXXFLAGS='-std=c++23 -Wall -Wextra -g -fsanitize=thread' \
  LDLIBS='-lpthread -fsanitize=thread' test_ipc test_cancellation
```

Run the resulting binaries with `UBSAN_OPTIONS=halt_on_error=1` for the
ASan/UBSan build. The storage QEMU smoke additionally verifies queued peer
exit, accepted server exit, accepted/queued caller cancellation, a late reply
with a second caller already queued, successful subsequent exchange, explicit
clone-stack unmapping and return to the baseline thread count. All three boots
require `storage_smoke: IPC cancellation and peer teardown PASS`. These target
checks run on one CPU; the dedicated SMP retirement gate is described below.
No IPC deadline API or
complete POSIX signal handlers/restart policy is added.

## SMP dispatch ownership and thread retirement

`test_dispatch` links the production shared dispatch/queue implementation.
Nine tests cover retained outgoing stacks, selected fatal candidates, peer pins,
wakes before/during/after a switch, duplicate wake suppression, priority/FIFO
behavior and work stealing. A concurrent campaign models four CPUs, 32 threads,
a waker and a reaper over 8,000 scheduling steps. It checks that a stack has at
most one executing CPU and that no executing thread passes the reclamation gate.
Another case frees a heap thread after the incoming tail, then repeats the tail
under ASan to detect a retained outgoing pointer. Hosted tests do not perform
real context switches or privileged IRQ masking.

```sh
make -C tests test_dispatch
build/host-tests/test_dispatch
make -C tests OBJDIR=../build/host-tests/retirement-sanitized \
  CXXFLAGS='-std=c++23 -Wall -Wextra -g -fsanitize=address,undefined' \
  LDLIBS='-lpthread -fsanitize=address,undefined' test_dispatch test_ipc test_cancellation
UBSAN_OPTIONS=halt_on_error=1 build/host-tests/retirement-sanitized/test_dispatch
make -C tests OBJDIR=../build/host-tests/retirement-tsan \
  CXXFLAGS='-std=c++23 -Wall -Wextra -g -fsanitize=thread' \
  LDLIBS='-lpthread -fsanitize=thread' test_dispatch
build/host-tests/retirement-tsan/test_dispatch
```

All run queues and CPU ownership claims use a shared IRQ-safe dispatch lock;
there is no lock held across a context switch. Selection claims an off-CPU
thread before inspecting fatal signals or preparing its address space. The
outgoing thread remains claimed until `scheduler_finish_switch`, called by the
amd64/ARMv7/AArch64 assembly on the incoming stack before register restoration
and IRQ enablement. Only that tail publishes it off CPU and queues a runnable
thread. Early wakes set READY without enqueueing an executing stack; repeated
wakes are idempotent, and an atomic BLOCKED-to-READY transition preserves exit
publication. Work stealing can take a single off-CPU queued thread.

Both cleanup and waitpid require TERMINATED, off CPU, off queue and zero pins
before destroying a thread's stack or address space. waitpid may consume status
before the handoff; the corpse remains in the global list for later cleanup and
shared-mm accounting. A selected fatal candidate retains its claim through
cleanup and parent notification. The obsolete shared kernel-stack scratch value
is removed; amd64 syscall stack offsets have compile-time assertions.

`scripts/test_smoke_aarch64.sh` now requires the smoke-product-only retirement
probe in its four-CPU run. Eight workers must demonstrate execution on all four
CPUs (mask 15). A deliberately held terminated worker keeps executing with IRQs
masked while a remote controller consumes its wait status once and tries
cleanup. It must remain discoverable until released and switched away. Then 48
rounds exercise queued peer death, accepted server exit, accepted caller
cancellation, a late reply with another caller queued, subsequent success and
waitpid. Every child must disappear from the thread list, and queue links/counts
are checked after each round. The final marker is
`SMP retirement: PASS (4 CPUs, mask 15, 48 IPC/exit/cancel/wait rounds)`.
One-CPU runs explicitly skip this probe and retain the prior boot/syscall gates.
QEMU uses `-nic none`; no host network socket or physical board is used.

This gate exercises kernel-worker stacks and IPC teardown on AArch64. The
amd64 storage/compatibility and ARMv7 boot gates exercise their switch tails on
one CPU. This does not establish amd64/ARMv7 SMP readiness, CPU hot-unplug,
concurrent userspace remap/TLB correctness or every wait-queue lifetime. Shared
dispatch serialization favors a reviewable ownership boundary; no scaling or
performance claim is made.

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
