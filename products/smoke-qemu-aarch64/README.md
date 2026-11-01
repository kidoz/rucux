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
usual userspace probes. It explicitly skips on one CPU. It exercises kernel
workers, not simultaneous userspace remap/access or remote TLB shootdowns. No
physical-board, hot-unplug, latency or throughput claim follows from this gate.
See [hosted test coverage](../../tests/README.md) for the dispatch ownership and
reclamation contract and sanitizer commands.
