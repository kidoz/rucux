#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run offline one/four-CPU ARMv7 kernel scheduling and broadcast-TLB gates."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import time
from datetime import UTC, datetime
from pathlib import Path

from test_retirement_smoke import workspace_build

ROOT = Path(__file__).resolve().parent.parent


def required_markers(cpus: int) -> tuple[str, ...]:
    mask = (1 << cpus) - 1
    markers = (
        f"rucux (armv7) boot complete, {cpus} CPUs online",
        f"ARMv7 TLB: PASS ({cpus} CPUs, mask {mask}, 64 replace/protect/unmap/reuse rounds)",
        "ARMv7 TLB: inactive root teardown PASS",
        f"ARMv7 scheduler: PASS (CPU mask {mask}, 8 workers, 8192 yields)",
        "ARMv7 scheduler: timer preemption PASS (8 workers)",
    ) + tuple(f"CPU{id}: timer IRQ verified" for id in range(cpus))
    if cpus == 1:
        return markers + ("ARMv7 TLB: broadcast SKIP (one CPU)",)
    return markers + ("SMP: 3 secondary cores ready", "conduit hvc")


def failed(text: str) -> bool:
    return any(
        marker in text
        for marker in (
            "FAIL",
            "TLB mismatch",
            "Data Abort",
            "Prefetch Abort",
            "Undefined Instruction",
            "FIQ (not supported)",
            "PANIC",
            "Invalid block magic",
        )
    )


def accepted(text: str, cpus: int, exit_code: int | None) -> bool:
    return exit_code == 0 and not failed(text) and all(m in text for m in required_markers(cpus))


def attempt(work: Path, kernel: Path, cpus: int, timeout: float) -> dict[str, object]:
    serial = work / "serial.log"
    command = [
        "qemu-system-arm",
        "-accel",
        "tcg,thread=multi",
        "-M",
        "virt",
        "-cpu",
        "cortex-a15",
        "-smp",
        str(cpus),
        "-m",
        "512M",
        "-kernel",
        str(kernel),
        "-serial",
        f"file:{serial}",
        "-display",
        "none",
        "-monitor",
        "stdio",
        "-nic",
        "none",
        "-no-reboot",
    ]
    (work / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    started = time.monotonic()
    complete = False
    with (work / "monitor.log").open("w") as monitor:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=monitor, stderr=subprocess.STDOUT, text=True)
        try:
            while time.monotonic() - started < timeout:
                text = serial.read_text(errors="replace") if serial.exists() else ""
                if failed(text):
                    time.sleep(0.05)  # Let the first bounded diagnostic line finish.
                    break
                if all(marker in text for marker in required_markers(cpus)):
                    complete = True
                    break
                if process.poll() is not None:
                    break
                time.sleep(0.02)
            if process.poll() is None and process.stdin:
                if not complete:
                    process.stdin.write(
                        "stop\ninfo cpus\n" + "".join(f"cpu {n}\ninfo registers\n" for n in range(cpus))
                    )
                    process.stdin.flush()
                    time.sleep(0.15)
                    ranges = [("kernel-memory.bin", 0x40000000, 16777216), ("heap-memory.bin", 0x5F000000, 16777216)]
                    tables = re.search(r"LPAE enabled, L1 at 0x([0-9A-Fa-f]+)", text)
                    if tables:
                        table_base = int(tables[1], 16) & ~0xFFFFF
                        if 0x40000000 <= table_base <= 0x5FE00000:
                            ranges.append(("table-memory.bin", table_base, 2097152))
                    (work / "memory-ranges.json").write_text(json.dumps(ranges, indent=2) + "\n")
                    for name, address, size in ranges:
                        target = work / name
                        process.stdin.write(f'pmemsave {address:#x} {size} "{target}"\n')
                        process.stdin.flush()
                        deadline = time.monotonic() + 3
                        while time.monotonic() < deadline:
                            if target.exists() and target.stat().st_size == size:
                                break
                            time.sleep(0.02)
                process.stdin.write("quit\n")
                process.stdin.flush()
                process.wait(timeout=10)
        except BrokenPipeError, subprocess.TimeoutExpired:
            complete = False
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    text = serial.read_text(errors="replace") if serial.exists() else ""
    return {
        "cpus": cpus,
        "pass": complete and accepted(text, cpus, process.returncode),
        "exit_code": process.returncode,
        "elapsed_seconds": round(time.monotonic() - started, 3),
        "failure_marker": failed(text),
        "missing": [m for m in required_markers(cpus) if m not in text],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/smoke-smp-qemu-armv7"))
    parser.add_argument("--output-dir", type=Path, default=Path("build/armv7-smp-evidence"))
    parser.add_argument("--attempts", type=int, default=20)
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    if not 1 <= args.attempts <= 10000 or not 0 < args.timeout <= 300:
        parser.error("attempts/timeout outside supported bounds")
    try:
        build, output = workspace_build(args.build_dir), workspace_build(args.output_dir)
    except ValueError as error:
        parser.error(str(error))
    for command in ("meson", "arm-none-eabi-objcopy", "qemu-system-arm"):
        if shutil.which(command) is None:
            parser.error(f"missing {command}; nothing is fetched")
    work = output / datetime.now(UTC).strftime("%Y%m%dT%H%M%S.%fZ")
    work.mkdir(parents=True)
    with (work / "build.log").open("w") as log:
        setup = ["meson", "setup", str(build), "--cross-file", "cross_armv7.txt", "-Dproduct=smoke-smp-qemu-armv7"]
        if build.exists():
            setup.append("--reconfigure")
        for cmd in (setup, ["meson", "compile", "-C", str(build)]):
            subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    shutil.copyfile(build / "kernel.elf", work / "kernel.elf")
    kernel = work / "kernel.bin"
    subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", str(work / "kernel.elf"), str(kernel)], check=True)
    metadata = {
        "schema": 1,
        "kernel_sha256": hashlib.sha256((work / "kernel.elf").read_bytes()).hexdigest(),
        "binary_sha256": hashlib.sha256(kernel.read_bytes()).hexdigest(),
        "qemu": subprocess.check_output(["qemu-system-arm", "--version"], text=True).strip(),
        "git_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "network": "-nic none",
        "timeout_seconds": args.timeout,
        "failure_memory_ranges": [[0x40000000, 16777216], [0x5F000000, 16777216]],
    }
    (work / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    results = []
    for n, cpus in enumerate([1] + [4] * args.attempts):
        directory = work / f"boot-{n:04d}-{cpus}cpu"
        directory.mkdir()
        result = attempt(directory, kernel, cpus, args.timeout)
        results.append(result)
        (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(f"armv7-smp: boot {n} ({cpus} CPUs) {'PASS' if result['pass'] else 'FAIL'}; {directory}", flush=True)
        if not result["pass"]:
            return 1
    print(f"armv7-smp: PASS (one compatibility boot, {args.attempts} SMP boots); {work}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
