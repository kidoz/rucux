#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check FAT32 truncate/write/fsync through libc over three isolated QEMU boots."""

from __future__ import annotations

import argparse
import json
import math
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def run(args: list[str]) -> None:
    subprocess.run(args, check=True, cwd=ROOT)


def extract(image: Path, work: Path, name: str, stage: int) -> bytes:
    output = work / f"stage-{stage}-{name}"
    run(["mcopy", "-o", "-i", str(image), f"::/{name}", str(output)])
    return output.read_bytes()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/smoke-storage-qemu-amd64")
    parser.add_argument("--bootloader", type=Path, default=ROOT / "bootloader/BOOTX64.EFI")
    parser.add_argument("--uefi-code", type=Path, default=Path("/opt/homebrew/share/qemu/edk2-x86_64-code.fd"))
    parser.add_argument("--uefi-vars", type=Path, default=Path("/opt/homebrew/share/qemu/edk2-i386-vars.fd"))
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    args.bootloader = args.bootloader.resolve()
    args.uefi_code = args.uefi_code.resolve()
    args.uefi_vars = args.uefi_vars.resolve()
    build = args.build_dir.resolve()
    kernel = build / "kernel.elf"
    options = build / "meson-info/intro-buildoptions.json"
    if not options.is_file() or not any(
        option.get("name") == "product" and option.get("value") == "smoke-storage-qemu-amd64"
        for option in json.loads(options.read_text())
    ):
        parser.error("--build-dir must be configured for smoke-storage-qemu-amd64")
    for source in (kernel, args.bootloader, args.uefi_code, args.uefi_vars):
        if not source.is_file():
            parser.error(f"missing prerequisite: {source}")
    work = build / "storage-smoke"
    work.mkdir(parents=True, exist_ok=True)
    image = work / "uefi.img"
    with image.open("wb") as stream:
        stream.truncate(48 * 1024 * 1024)
    run(["mformat", "-i", str(image), "-F", "::"])
    run(["mmd", "-i", str(image), "::/EFI", "::/EFI/BOOT"])
    run(["mcopy", "-i", str(image), str(args.bootloader), "::/EFI/BOOT/BOOTX64.EFI"])
    run(["mcopy", "-i", str(image), str(kernel), "::/kernel.elf"])
    seed = bytes((i * 13 + 17) % 256 for i in range(2048))
    for name, contents in (("shrink.bin", seed[:1024]), ("rewrite.bin", seed), ("stage.bin", b"0")):
        source = work / name
        source.write_bytes(contents)
        run(["mcopy", "-i", str(image), str(source), f"::/{name}"])
    variables = work / "uefi-vars.fd"
    shutil.copyfile(args.uefi_vars, variables)
    payload = bytes((i * 29 + 101) % 256 for i in range(700))
    for stage in (1, 2, 3):
        log = work / f"boot-{stage}.log"
        with log.open("wb") as stream:
            try:
                result = subprocess.run(
                    [
                        "qemu-system-x86_64",
                        "-drive",
                        f"if=pflash,format=raw,readonly=on,file={args.uefi_code}",
                        "-drive",
                        f"if=pflash,format=raw,file={variables}",
                        "-drive",
                        f"file={image},if=ide,format=raw",
                        "-smp",
                        "1",
                        "-m",
                        "256M",
                        "-serial",
                        "stdio",
                        "-display",
                        "none",
                        "-monitor",
                        "none",
                        "-nic",
                        "none",
                        "-no-reboot",
                    ],
                    stdout=stream,
                    stderr=subprocess.STDOUT,
                    timeout=args.timeout,
                    check=False,
                )
            except subprocess.TimeoutExpired as error:
                raise RuntimeError(f"stage {stage}: QEMU timed out; see {log}") from error
        text = log.read_text(errors="replace")
        if (
            result.returncode
            or f"storage_smoke: PASS stage={stage}" not in text
            or any(marker in text for marker in ("storage_smoke: FAIL", "PANIC", "Page Fault", "Exception"))
        ):
            raise RuntimeError(f"stage {stage}: runtime check failed; see {log}")
        if extract(image, work, "shrink.bin", stage) != seed[:37]:
            raise RuntimeError(f"stage {stage}: nonzero shrink was not persisted")
        expected = b"" if stage == 2 else payload
        if extract(image, work, "rewrite.bin", stage) != expected:
            raise RuntimeError(f"stage {stage}: rewrite contents or EOF did not persist")
        if extract(image, work, "stage.bin", stage) != str(stage).encode():
            raise RuntimeError(f"stage {stage}: checkpoint did not persist")
        print(f"storage-smoke: stage {stage} PASS", flush=True)
    print(f"storage-smoke: PASS (3 boots, no host network); artifacts: {work}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
