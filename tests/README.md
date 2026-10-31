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
make -C tests test_fat32 test_ata
build/host-tests/test_fat32
build/host-tests/test_ata
build/host-tests/test_fat32 --crash 1 1 1 before
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

The ATA suite links `src/arch/amd64/ata.cpp` against the simulated port interface
in `tests/support/arch/amd64/io.hpp`. It checks transfer/flush errors, device
faults, polling bounds, LBA28 request validation, IDENTIFY and command reservation.
Run both suites under ASan/UBSan with:

```sh
make -C tests OBJDIR=../build/host-tests/fat32-sanitized \
  CXXFLAGS='-std=c++23 -Wall -Wextra -g -fsanitize=address,undefined' \
  LDLIBS='-lpthread -fsanitize=address,undefined' test_fat32 test_ata
build/host-tests/fat32-sanitized/test_fat32
build/host-tests/fat32-sanitized/test_ata
```

These tests assume ordered, atomic 512-byte sectors and a device honoring flush
completion. Failed writes may persist unacknowledged bytes. There is no promise
of whole-write atomicity, torn-sector recovery, reordered unflushed writes,
cluster allocation, sparse writes, or full POSIX fsync/ftruncate semantics.
The existing logical truncate behavior is retained, but truncation alone is not
made durable by this change. Concurrent operations fail without waiting through
the current 0/null/false interfaces; this is not full concurrent filesystem support.
