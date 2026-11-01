# ARMv7 SMP kernel memory gate

```sh
python3 tools/test_armv7_smp_smoke.py --attempts 30
```

This product runs kernel-only probes on QEMU `virt`/Cortex-A15 with 512 MiB RAM.
The helper builds the matching product, runs one single-CPU compatibility boot,
then the requested number of four-CPU boots. It uses parallel TCG and `-nic none`,
opens no host sockets and downloads nothing. ARMv7 userspace is not enabled.

The gate requires:

- Bounded FDT discovery of enabled CPU affinities and the PSCI conduit. Duplicate
  affinities, unsupported methods and malformed ranges are rejected; startup
  uses the discovered MPIDRs rather than assuming contiguous hardware IDs.
- PSCI HVC startup of three APs with private SVC/exception stacks, a shared
  LPAE root, coherency, CPU-local GIC/timer state and scheduler idle contexts.
- Every CPU enters a private shared root with IRQs masked. Across 64 rounds,
  readers prime a mapping, check read-only permissions with address-translation
  instructions, observe a replacement frame, and check that unmapping faults.
  Replaced/unmapped frames are actually freed, reallocated and poisoned before
  the next check. Readers only sample stable phases, not the transient BBM gap.
- The private root leaves every CPU's active-root registry before its tables
  are discarded. A failure retains the mappings/frames and parked CPUs.
- Eight kernel workers each observe a timer interrupt or migration while doing
  no voluntary yielding, preserve identity and stack canaries, then complete
  8,192 yields with coverage of every CPU. IRQ entry saves the exception return
  state onto the thread's SVC stack before scheduling; it travels with the thread.

The one-CPU run requires local memory, root teardown and scheduler success plus
an explicit skip of the remote broadcast claim. Four-CPU success requires all
four individual timer markers, the full memory/coverage masks and all eight
preemption observations. Boot counts or a normal QEMU monitor exit are insufficient.

Each campaign creates a unique directory under `build/armv7-smp-evidence/` with
the exact ELF/raw image, hashes, QEMU version, commands, serial/monitor logs and
incremental results. Failures stop all CPUs and capture registers, bounded kernel
and heap RAM, and a page-table region derived from the logged bootstrap root.
`memory-ranges.json` records captured ranges. These are not complete RAM dumps.

The negative-control check replaces broadcast TLBIALLIS with local TLBIALL while
retaining descriptor stores and DSB/ISB. The four-CPU probe must reject stale
remote translations; the deliberately altered code belongs only under `build/`.

The kernel reserves both its boot region and the actual FDT allocation before
PMM initialization. Manifest defaults supply platform addresses when appropriate;
FDT memory/GIC/UART discovery takes precedence. RAM uses Normal inner-shareable
attributes; non-RAM identity mappings use Device/XN attributes. AP table walks
join the BSP's inner-shareable cache/coherency configuration.

Coverage is QEMU Cortex-A15, one/four CPUs, PSCI 0.2+ and GICv2. It does not prove
physical-board cache ordering, CPU hotplug, arbitrary firmware/modes, ASID
retention, userspace/EL0, concurrent readers inside a BBM gap or performance.
