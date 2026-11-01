# AArch64 boot and SMP retirement smoke

Run from the repository root with the existing local cross-toolchain and QEMU:

```sh
RUCUX_AARCH64_SMOKE_TIMEOUT=8 bash scripts/test_smoke_aarch64.sh
```

The script builds this product, then runs QEMU `virt`/Cortex-A53 with one and
four CPUs and `-nic none`. Logs default to
`build/logs/smoke-qemu-aarch64/{boot,smp}.log`. Both guests remain running after
their checks, so bounded timeouts are expected; required/forbidden serial
markers determine success. Nothing is fetched or installed.

The four-CPU smoke requires MMU replacement/permissions, all secondary CPUs and
their timers, userspace scheduling/syscall safety, and the kernel retirement
probe. The probe demonstrates work on every CPU, holds an outgoing terminated
stack while another CPU consumes its status and attempts reclamation, and runs
48 rounds combining IPC, peer exit, caller cancellation, late replies and
waitpid. Every child must be reclaimed and the queue structure must stay valid.
The required marker is
`SMP retirement: PASS (4 CPUs, mask 15, 48 IPC/exit/cancel/wait rounds)`.

The retirement probe is compiled only into this product and runs before the
usual userspace probes. It explicitly skips on one CPU. Its CPU coverage counts
workers and controller; local affinity need not migrate workers onto every CPU.

The product also runs `mm_smp_smoke` at EL0 on one and four CPUs. Four waves of
four clone workers exercise 64 map/reuse rounds, concurrent sibling mappings,
1,024 permission changes and reads during break-before-make. Readers pause
before unmapping; recycled frames are held and poisoned at another address to
make stale translations observable. All clones must be reaped before their
stacks are unmapped. Required markers include
`shared mm: userspace map/protect/reuse/clone PASS` and
`shared mm: root teardown PASS (CPU mask 15)` (mask 1 on one CPU). The latter is
emitted after last-owner table reclamation and proves the shared root ran on all
selected CPUs. Native broadcast TLBI plus DSB completes before frame reuse.

No physical-board, hot-unplug, latency or throughput claim follows from this gate.
See [hosted test coverage](../../tests/README.md) for the dispatch ownership and
reclamation contract and sanitizer commands.
