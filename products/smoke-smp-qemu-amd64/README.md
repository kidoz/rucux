# amd64 SMP memory smoke product

This product embeds `mm_smp_smoke` and `syscall_smoke` without service daemons.
It validates amd64 AP startup, per-CPU timers/scheduling, retirement, shared
userspace mappings and remote NMI TLB completion in QEMU.

```sh
python3 tools/test_amd64_smp_smoke.py --attempts 20
```

The helper rebuilds the amd64 UEFI loader and this product, then runs one
single-CPU compatibility boot followed by the requested number of four-CPU
boots. It uses parallel TCG, `-cpu max`, 512 MiB RAM and `-nic none`. It fetches
nothing and opens no host sockets. Firmware files default to the installed
Homebrew QEMU paths; override `--uefi-code` and `--uefi-vars` when necessary.

Each campaign has a unique directory under `build/amd64-smp-evidence/` with the
exact kernel and loader, their SHA-256 hashes, QEMU version, commands, boot
image, private NVRAM, serial/monitor logs and incremental results. Disk writes
use temporary snapshot overlays in the attempt directory. Before quitting a
failed guest, the helper stops all CPUs and captures registers plus bounded
kernel and heap memory. A clean monitor exit alone never counts as success;
all required guest markers must be present and faults/failure markers absent.

The four-CPU gate requires:

- All three APs complete descriptor-table, GS, syscall, root and idle-thread
  initialization and deliver a local timer IRQ.
- With ordinary IRQs masked (checked from RFLAGS), each AP primes a mapping.
  The BSP replaces it through the normal NMI mailbox path and poisons the old
  frame after acknowledgement. Every AP must observe the new frame. A timeout
  or mismatch preserves the failing mappings and parked APs.
- The kernel retirement probe completes 48 exit/IPC/cancellation/wait rounds
  with execution on all four CPUs and stack-held thread-identity checks.
- Four waves of four userspace clones complete 64 map/reuse rounds and 1,024
  protection changes, including kernel copyout permission checks. Readers pause
  before unmap. Successful remote acknowledgements for this workload must target
  every CPU, and final root teardown must report execution mask 15.
- Userspace syscall safety passes. The single-CPU gate requires corresponding
  workload/root-teardown success and explicit skips of remote-only checks.

The loader passes ACPI configuration tables and reserves the SIPI page at
physical `0x8000` before leaving firmware. Boot protocol V2 appends this page to
the unchanged V1 prefix. The kernel accepts V1 for one-CPU compatibility and
never reads its missing tail; AP startup fails closed when no reserved page is
available. Rebuild the loader when updating this handoff.

This is evidence for the tested xAPIC/legacy I/O APIC QEMU topology. It does not
establish physical-board, x2APIC, CPU hotplug, PCID retention, arbitrary service
or device SMP safety, formal ordering proofs or performance/scaling guarantees.
