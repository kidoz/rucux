# Tools

This directory is reserved for host-side helper tools that are larger or more product-oriented than the generic scripts in `scripts/`.

Examples:

- product assembly helpers
- release packaging tools
- manifest validation helpers
- board or product inspection tools

Keep small, generic helper scripts in `scripts/`. Use `tools/` when the helper becomes part of the workspace and distribution model.

## Python quality checks

Python 3.14+ and `uv` are required. Runtime scripts remain standard-library-only;
Ruff and mypy are development dependencies in `pyproject.toml`.

```sh
just py-sync       # Install the locked development/build/documentation tools
just py-check      # Ruff lint, mypy, and Ruff formatting check
just py-format     # Apply Python formatting
```

`just py-lint` and `just py-typecheck` run each check separately. `just check`
also includes `py-check` alongside the existing C++ checks. The Python checks
are read-only and run offline with `uv --no-sync`; run `py-sync` first to prepare
`.venv`. The sync command may download dependencies.

Both tools cover project scripts in `tools/` and `scripts/`. Generated outputs,
third-party ports, and agent tooling are outside this scope. Ruff checks Python
errors, imports, and modernization rules, and provides the formatter. Mypy checks
typed interfaces and the bodies of existing untyped functions; this is gradual
typing, not strict mode. Service policies and trace events use typed dictionaries,
and the Ethernet test peer has typed interfaces.

Tool caches are stored under `build/python-tools/`. CI runs the same checks
exposed locally through `just`.

## AArch64 retirement campaigns

Run a bounded four-CPU scheduler/IPC retirement campaign without host networking:

```sh
python3 tools/test_retirement_smoke.py --seed 37 --rounds 4096 --attempts 20
```

The helper configures `build/retirement-investigation` for `smoke-qemu-aarch64`.
Meson options `retirement_seed` and `retirement_rounds` default to 0 and 48;
seed 0 disables the extra actor-local yield checkpoints. Runtime dependencies
must already be installed; the helper fetches nothing.

Each campaign creates a unique directory under `build/retirement-evidence/`
containing the exact ELF, its SHA-256, QEMU/host configuration, commands, serial
output, monitor output and incremental results. All success markers, including
thread identity, IPC retirement, userspace shared-mm access and final root
teardown, are mandatory. An assertion freezes the event rings and keeps the
controller alive before cleanup. On failure or timeout the helper stops QEMU,
captures all CPU registers and two 16 MiB guest-memory ranges (kernel metadata
and the upper allocator region), then quits through its private stdin monitor.
These bounded snapshots are not a complete guest-memory dump.

Replay an archived ELF with matching seed and round count:

```sh
python3 tools/test_retirement_smoke.py --kernel build/retirement-evidence/<campaign>/kernel.elf \
    --seed 37 --rounds 4096 --attempts 20
```

The seed reproduces yield choices, not an exact SMP interleaving. Parallel TCG
is the default. `--tcg-threads single --icount 3` provides an instruction-counted
alternative; it does not prove hardware ordering. All build and artifact paths
must stay inside the workspace's `build/` directory.

Check the helper's evidence policy with:

```sh
python3 -m unittest discover -s tools -p test_retirement_smoke_unit.py
```

## amd64 SMP memory gate

```sh
python3 tools/test_amd64_smp_smoke.py --attempts 20
python3 -m unittest discover -s tools -p test_amd64_smp_smoke_unit.py
```

The gate checks a single-CPU boot and repeated four-CPU boots, including AP timer
activity, IRQ-masked remote NMI replacement, userspace shared-mm stress and final
root teardown. It rebuilds the matching loader and kernel and keeps unique,
network-free evidence under `build/amd64-smp-evidence/`. See the
[product contract](../products/smoke-smp-qemu-amd64/README.md) for required markers,
boot protocol compatibility, failure captures and limits.

## ARMv7 SMP kernel gate

```sh
python3 tools/test_armv7_smp_smoke.py --attempts 30
python3 -m unittest discover -s tools -p test_armv7_smp_smoke_unit.py
```

This offline gate requires one/four-CPU kernel scheduling, timer preemption,
broadcast-TLB replacement/protection/unmap/reuse and inactive-root teardown.
Exact binaries and failure snapshots remain under `build/armv7-smp-evidence/`.
See the [product contract](../products/smoke-smp-qemu-armv7/README.md) for the
required evidence and ARMv7's kernel-only scope.
