#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate amd64 APs, masked-IRQ remote NMI and userspace shared-mm reclamation offline."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import time
from datetime import UTC, datetime
from pathlib import Path

from test_retirement_smoke import workspace_build

ROOT = Path(__file__).resolve().parent.parent


def required_markers(cpus: int) -> tuple[str, ...]:
    common = (
        f"rucux (amd64) boot complete, {cpus} CPUs online",
        "TLB: NMI self-check PASS (IRQs masked)",
        "shared mm: userspace map/protect/reuse/clone PASS",
        f"shared mm: root teardown PASS (CPU mask {(1 << cpus) - 1})",
        "syscall safety: PASS",
    )
    if cpus == 1:
        return common + (
            "TLB: remote IRQ-masked replacement SKIP (one CPU)",
            "TLB: userspace remote shootdown SKIP (one CPU)",
            "SMP retirement: SKIP (requires multiple CPUs)",
        )
    return common + (
        "SMP: 3 secondary cores ready",
        "AP1: timer IRQ verified",
        "AP2: timer IRQ verified",
        "AP3: timer IRQ verified",
        "TLB: remote IRQ-masked replacement PASS (CPU mask 14)",
        "TLB: userspace remote shootdown PASS (target mask 15,",
        "SMP retirement: thread identity verified",
        "SMP retirement: held outgoing stack and single wait status verified",
        "SMP retirement: PASS (4 CPUs, mask 15, 48 IPC/exit/cancel/wait rounds)",
    )


def failed(text: str) -> bool:
    return any(
        marker in text
        for marker in (
            "FAIL",
            "SMP retirement: check",
            "EXCEPTION ",
            "#PF at ",
            "Fault RIP=",
            "PANIC",
            "Invalid block magic",
        )
    )


def accepted(text: str, cpus: int, exit_code: int | None) -> bool:
    return exit_code == 0 and not failed(text) and all(marker in text for marker in required_markers(cpus))


def attempt(work: Path, image: Path, firmware: Path, variables: Path, cpus: int, timeout: float) -> dict[str, object]:
    shutil.copyfile(variables, work / "vars.fd")
    serial = work / "serial.log"
    command = [
        "qemu-system-x86_64",
        "-accel",
        "tcg,thread=multi",
        "-cpu",
        "max",
        "-drive",
        f"if=pflash,format=raw,readonly=on,file={firmware}",
        "-drive",
        f"if=pflash,format=raw,file={work / 'vars.fd'}",
        "-drive",
        f"file={image},if=ide,format=raw,snapshot=on",
        "-smp",
        str(cpus),
        "-m",
        "512M",
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
    start = time.monotonic()
    complete = False
    with (work / "monitor.log").open("w") as monitor:
        process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=monitor,
            stderr=subprocess.STDOUT,
            text=True,
            env={**os.environ, "TMPDIR": str(work)},
        )
        try:
            while time.monotonic() - start < timeout:
                text = serial.read_text(errors="replace") if serial.exists() else ""
                if failed(text):
                    if "SMP retirement: check" in text and "retirement diagnostics: end" not in text:
                        time.sleep(0.02)
                        continue
                    break
                if all(marker in text for marker in required_markers(cpus)):
                    complete = True
                    break
                if process.poll() is not None:
                    break
                time.sleep(0.02)
            if process.poll() is None and process.stdin:
                if not complete:
                    commands = "stop\ninfo cpus\n" + "".join(f"cpu {n}\ninfo registers\n" for n in range(cpus))
                    process.stdin.write(commands)
                    process.stdin.flush()
                    time.sleep(0.15)
                    # Keep bounded kernel metadata and allocator/stack regions.
                    heap_match = re.search(r"Kernel Heap Initialized at 0x([0-9A-Fa-f]+)", text)
                    heap = int(heap_match[1], 16) if heap_match else 0x1B000000
                    heap = min(heap & ~4095, 0x1F000000)
                    for name, address in (("kernel-memory.bin", 0x200000), ("heap-memory.bin", heap)):
                        target = work / name
                        process.stdin.write(f'pmemsave {address:#x} 16777216 "{target}"\n')
                        process.stdin.flush()
                        deadline = time.monotonic() + 3
                        while time.monotonic() < deadline:
                            if target.exists() and target.stat().st_size == 16777216:
                                break
                            time.sleep(0.02)
                process.stdin.write("quit\n")
                process.stdin.flush()
                process.wait(timeout=3)
        except BrokenPipeError:
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
        "elapsed_seconds": round(time.monotonic() - start, 3),
        "missing": [m for m in required_markers(cpus) if m not in text],
        "failure_marker": failed(text),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/smoke-smp-qemu-amd64"))
    parser.add_argument("--output-dir", type=Path, default=Path("build/amd64-smp-evidence"))
    parser.add_argument(
        "--attempts", type=int, default=10, help="Four-CPU boots after one single-CPU compatibility boot"
    )
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--uefi-code", type=Path, default=Path("/opt/homebrew/share/qemu/edk2-x86_64-code.fd"))
    parser.add_argument("--uefi-vars", type=Path, default=Path("/opt/homebrew/share/qemu/edk2-i386-vars.fd"))
    args = parser.parse_args()
    if not 1 <= args.attempts <= 10000 or not 0 < args.timeout <= 300:
        parser.error("attempts/timeout outside supported bounds")
    try:
        build, output = workspace_build(args.build_dir), workspace_build(args.output_dir)
    except ValueError as error:
        parser.error(str(error))
    for command in ("meson", "make", "qemu-system-x86_64", "mformat", "mmd", "mcopy"):
        if shutil.which(command) is None:
            parser.error(f"missing {command}; nothing is fetched")
    if not args.uefi_code.is_file() or not args.uefi_vars.is_file():
        parser.error("UEFI code/variables are required; nothing is fetched")
    work = output / datetime.now(UTC).strftime("%Y%m%dT%H%M%S.%fZ")
    work.mkdir(parents=True)
    with (work / "build.log").open("w") as log:
        setup = ["meson", "setup", str(build), "--cross-file", "cross_amd64.txt", "-Dproduct=smoke-smp-qemu-amd64"]
        if build.exists():
            setup.append("--reconfigure")
        for cmd in (["make", "-C", "bootloader", "ARCH=x86_64"], setup, ["meson", "compile", "-C", str(build)]):
            subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    shutil.copyfile(build / "kernel.elf", work / "kernel.elf")
    shutil.copyfile(ROOT / "bootloader/BOOTX64.EFI", work / "BOOTX64.EFI")
    image = work / "uefi.img"
    with image.open("wb") as stream:
        stream.truncate(48 * 1024 * 1024)
    for cmd in (
        ["mformat", "-i", str(image), "-F", "::"],
        ["mmd", "-i", str(image), "::/EFI", "::/EFI/BOOT"],
        ["mcopy", "-i", str(image), str(work / "BOOTX64.EFI"), "::/EFI/BOOT/BOOTX64.EFI"],
        ["mcopy", "-i", str(image), str(work / "kernel.elf"), "::/kernel.elf"],
    ):
        subprocess.run(cmd, cwd=ROOT, check=True)
    metadata = {
        "kernel_sha256": hashlib.sha256((work / "kernel.elf").read_bytes()).hexdigest(),
        "loader_sha256": hashlib.sha256((work / "BOOTX64.EFI").read_bytes()).hexdigest(),
        "git_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "qemu": subprocess.check_output(["qemu-system-x86_64", "--version"], text=True).strip(),
        "network": "-nic none",
        "timeout_seconds": args.timeout,
    }
    (work / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    results = []
    for n, cpus in enumerate([1] + [4] * args.attempts):
        directory = work / f"boot-{n:04d}-{cpus}cpu"
        directory.mkdir()
        result = attempt(directory, image, args.uefi_code.resolve(), args.uefi_vars.resolve(), cpus, args.timeout)
        results.append(result)
        (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(f"amd64-smp: boot {n} ({cpus} CPUs) {'PASS' if result['pass'] else 'FAIL'}; {directory}", flush=True)
        if not result["pass"]:
            return 1
    print(f"amd64-smp: PASS (one compatibility boot, {args.attempts} SMP boots); {work}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
