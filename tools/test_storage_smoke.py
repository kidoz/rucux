#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check FAT32 truncate/append/write/fsync through libc over three isolated QEMU boots."""

from __future__ import annotations

import argparse
import json
import math
import shutil
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def run(args: list[str]) -> None:
    subprocess.run(args, check=True, cwd=ROOT)


def extract(image: Path, work: Path, name: str, stage: int) -> bytes:
    output = work / f"stage-{stage}-{name}"
    run(["mcopy", "-o", "-i", str(image), f"::/{name}", str(output)])
    return output.read_bytes()


def file_sector(image: Path, short_name: bytes, offset: int, *, directory: bool = False) -> int:
    """Find a file sector or the first sector of a fixture directory without modifying it."""
    data = image.read_bytes()
    sector_size = int(struct.unpack_from("<H", data, 11)[0])
    sectors_per_cluster = data[13]
    reserved = int(struct.unpack_from("<H", data, 14)[0])
    fat_size = int(struct.unpack_from("<I", data, 36)[0])
    root = int(struct.unpack_from("<I", data, 44)[0])
    if (
        sector_size != 512
        or not sectors_per_cluster
        or offset < 0
        or offset % sector_size
        or len(short_name) != 11
        or (directory and offset != 0)
    ):
        raise RuntimeError("unsupported storage-smoke fixture geometry")
    cluster_bytes = sectors_per_cluster * sector_size
    fat_start = reserved * sector_size
    data_sector = reserved + data[16] * fat_size

    def cluster_position(cluster: int) -> int:
        position = (data_sector + (cluster - 2) * sectors_per_cluster) * sector_size
        if cluster < 2 or position < 0 or position + cluster_bytes > len(data):
            raise RuntimeError("storage-smoke fixture cluster is out of range")
        return position

    def next_cluster(cluster: int) -> int:
        if cluster * 4 + 4 > fat_size * sector_size:
            raise RuntimeError("storage-smoke FAT lookup is out of range")
        return int(struct.unpack_from("<I", data, fat_start + cluster * 4)[0]) & 0x0FFFFFFF

    visited: set[int] = set()
    while root < 0x0FFFFFF8 and root not in visited:
        visited.add(root)
        position = cluster_position(root)
        for entry in range(position, position + cluster_bytes, 32):
            if not data[entry]:
                raise RuntimeError("storage-smoke fixture entry was not found")
            if data[entry : entry + 11] != short_name:
                continue
            size = struct.unpack_from("<I", data, entry + 28)[0]
            if bool(data[entry + 11] & 0x10) != directory:
                raise RuntimeError("storage-smoke fixture has the wrong entry type")
            if not directory and offset >= size:
                raise RuntimeError("storage-smoke fixture offset is beyond EOF")
            cluster = (int(struct.unpack_from("<H", data, entry + 20)[0]) << 16) | int(
                struct.unpack_from("<H", data, entry + 26)[0]
            )
            file_visited: set[int] = set()
            for _ in range(offset // cluster_bytes):
                if cluster in file_visited or cluster >= 0x0FFFFFF8:
                    raise RuntimeError("invalid storage-smoke fixture chain")
                file_visited.add(cluster)
                cluster = next_cluster(cluster)
            return (cluster_position(cluster) + offset % cluster_bytes) // sector_size
        root = next_cluster(root)
    raise RuntimeError("storage-smoke fixture entry was not found")


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
    for name, contents in (
        ("shrink.bin", seed[:1024]),
        ("rewrite.bin", seed),
        ("append.bin", seed),
        ("stage.bin", b"0"),
        ("mmap.bin", bytes((i * 7 + 11) % 256 for i in range(8192 + 37))),
        # Keep firmware read-ahead for boot files away from the injected sector.
        ("mmapgap.bin", bytes(1024 * 1024)),
        ("mmaperr.bin", bytes((i * 19 + 73) % 256 for i in range(8192))),
    ):
        source = work / name
        source.write_bytes(contents)
        run(["mcopy", "-i", str(image), str(source), f"::/{name}"])
    run(["mmd", "-i", str(image), "::/faultdir"])
    run(["mcopy", "-i", str(image), str(work / "stage.bin"), "::/faultdir/known.bin"])
    variables = work / "uefi-vars.fd"
    shutil.copyfile(args.uefi_vars, variables)
    payload = bytes((i * 29 + 101) % 256 for i in range(700))
    fault_sector = file_sector(image, b"MMAPERR BIN", 4096)
    lookup_fault_sector = file_sector(image, b"FAULTDIR   ", 0, directory=True)
    disk = {
        "driver": "raw",
        "node-name": "storage",
        "file": {
            "driver": "blkdebug",
            "inject-error": [
                {"event": "read_aio", "errno": 5, "sector": sector, "once": False, "immediately": True}
                for sector in (fault_sector, lookup_fault_sector)
            ],
            "image": {"driver": "file", "filename": str(image)},
        },
    }
    (work / "storage-faults.json").write_text(json.dumps(disk, indent=2) + "\n")
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
                        "-blockdev",
                        json.dumps(disk),
                        "-device",
                        "ide-hd,drive=storage,bus=ide.0,unit=0",
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
            or "storage_smoke: mmap read-error rollback PASS" not in text
            or "storage_smoke: lookup errno and path validation PASS" not in text
            or "storage_smoke: vnode references and thread cleanup PASS" not in text
            or "storage_smoke: safe fatal syscall cancellation PASS" not in text
            or "storage_smoke: IPC cancellation and peer teardown PASS" not in text
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
        expected_append = (
            b"first\nsecond\nthird\n" + (b"boot2\n" if stage >= 2 else b"") + (b"boot3\n" if stage == 3 else b"")
        )
        if extract(image, work, "append.bin", stage) != expected_append:
            raise RuntimeError(f"stage {stage}: append records were overwritten or did not persist")
        print(f"storage-smoke: stage {stage} PASS", flush=True)
    print(f"storage-smoke: PASS (3 boots, no host network); artifacts: {work}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
