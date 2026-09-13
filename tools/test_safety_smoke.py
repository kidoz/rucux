#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the syscall/scheduling product offline, keeping artifacts in its build directory."""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("amd64", "aarch64"), required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--cpus", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=20)
    parser.add_argument("--uefi-code", type=Path)
    parser.add_argument("--uefi-vars", type=Path)
    parser.add_argument("--bootloader", type=Path)
    args = parser.parse_args()
    root = args.build_dir.resolve()
    repo = Path(__file__).resolve().parent.parent
    if not root.is_relative_to(repo) or not (root / "kernel.elf").is_file():
        parser.error("build-dir must contain a kernel.elf inside the repository")
    if args.cpus < 1 or args.timeout < 1:
        parser.error("cpus and timeout must be positive")
    if args.arch == "amd64":
        if not all(p and p.is_file() for p in (args.uefi_code, args.uefi_vars, args.bootloader)):
            parser.error("amd64 requires existing --uefi-code, --uefi-vars and --bootloader files")
        image = root / "safety-smoke.img"
        with image.open("wb") as output:
            output.truncate(48 * 1024 * 1024)
        commands = [
            ["mformat", "-i", str(image), "-F", "::"],
            ["mmd", "-i", str(image), "::/EFI", "::/EFI/BOOT"],
            ["mcopy", "-i", str(image), str(args.bootloader.resolve()), "::/EFI/BOOT/BOOTX64.EFI"],
            ["mcopy", "-i", str(image), str(root / "kernel.elf"), "::/kernel.elf"],
        ]
        for command in commands:
            subprocess.run(command, check=True)
        variables = root / "safety-smoke-vars.fd"
        shutil.copyfile(args.uefi_vars, variables)
        command = [
            "qemu-system-x86_64",
            "-drive", f"if=pflash,format=raw,readonly=on,file={args.uefi_code.resolve()}",
            "-drive", f"if=pflash,format=raw,file={variables}",
            "-drive", f"file={image},if=ide,format=raw",
        ]
    else:
        command = ["qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a53",
                   "-kernel", str(root / "kernel.elf")]
    command += ["-smp", str(args.cpus), "-m", "512M", "-serial", "stdio", "-display", "none",
                "-monitor", "none", "-nic", "none", "-no-reboot"]
    log_path = root / f"safety-smoke-{args.cpus}.log"
    status = 0
    with log_path.open("wb") as log:
        try:
            status = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout).returncode
        except subprocess.TimeoutExpired:
            pass  # The idle kernel does not exit; markers below determine success.
    log = log_path.read_text(errors="replace")
    required = ["syscall safety: PASS", "userspace: scheduling verified"]
    if args.arch == "aarch64":
        required.append(f"boot complete, {args.cpus} CPUs online")
        required += [f"AP{cpu}: timer IRQ verified" for cpu in range(1, args.cpus)]
    missing = [marker for marker in required if marker not in log]
    failures = [marker for marker in ("syscall safety: FAIL", "***", "PANIC", "Page Fault", "ELF load failed",
                                     "userspace: short write") if marker in log]
    if status or missing or failures:
        print(f"FAIL: status={status}, missing={missing}, failures={failures}; see {log_path}")
        return 1
    print(f"PASS: {args.arch}, {args.cpus} CPUs; see {log_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
