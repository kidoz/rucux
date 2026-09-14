#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

import product_info

REPO_ROOT = Path(__file__).resolve().parent.parent
PERSISTENT_JOURNAL_SIZE = 256 * 1024


class AssembleError(RuntimeError):
    pass


def run_command(args: list[str]) -> None:
    result = subprocess.run(args, cwd=REPO_ROOT, check=False)
    if result.returncode != 0:
        raise AssembleError(f"command failed: {' '.join(args)}")


def bootloader_path_from_image(image: dict[str, object]) -> Path:
    raw_path = image.get("efi_bootloader")
    if not isinstance(raw_path, str) or not raw_path:
        raise AssembleError("image manifest missing efi_bootloader")
    return REPO_ROOT / raw_path


def output_path_from_resolved(data: dict[str, object], image: dict[str, object]) -> Path:
    raw_output = image.get("output")
    if isinstance(raw_output, str) and raw_output:
        return REPO_ROOT / raw_output

    board = data.get("board")
    if not isinstance(board, dict):
        raise AssembleError("resolved board manifest is invalid")

    artifacts = board.get("artifacts")
    if not isinstance(artifacts, dict):
        raise AssembleError("board manifest missing artifacts")

    raw_board_output = artifacts.get("uefi_image")
    if not isinstance(raw_board_output, str) or not raw_board_output:
        raise AssembleError("board manifest missing artifacts.uefi_image")
    return REPO_ROOT / raw_board_output


def kernel_path_from_resolved(board: dict[str, object]) -> Path:
    override = os.environ.get("RUCUX_KERNEL_ARTIFACT")
    if override:
        kernel_path = Path(override)
        if not kernel_path.is_absolute():
            kernel_path = REPO_ROOT / kernel_path
        return kernel_path

    artifacts = board.get("artifacts")
    if not isinstance(artifacts, dict):
        raise AssembleError("board manifest missing artifacts")

    raw_kernel = artifacts.get("kernel")
    if not isinstance(raw_kernel, str) or not raw_kernel:
        raise AssembleError("board manifest missing artifacts.kernel")
    return REPO_ROOT / raw_kernel


def assemble_uefi_disk(data: dict[str, object], image: dict[str, object]) -> Path:
    image_size_mib = image.get("size_mib")
    if not isinstance(image_size_mib, str) and not isinstance(image_size_mib, int):
        raise AssembleError("image manifest missing size_mib")

    if not isinstance(image_size_mib, (str, int)):
        raise AssembleError("image.size_mib must be an integer")
    size_mib = int(image_size_mib)
    output_path = output_path_from_resolved(data, image)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    bootloader_path = bootloader_path_from_image(image)
    if not bootloader_path.exists():
        raise AssembleError(f"bootloader not found: {bootloader_path}")

    board = data.get("board")
    if not isinstance(board, dict):
        raise AssembleError("resolved board manifest is invalid")
    kernel_path = kernel_path_from_resolved(board)
    if not kernel_path.exists():
        raise AssembleError(f"kernel artifact not found: {kernel_path}")

    efi_boot_path = image.get("efi_boot_path")
    kernel_target = image.get("kernel_target")
    if not isinstance(efi_boot_path, str) or not efi_boot_path:
        raise AssembleError("image manifest missing efi_boot_path")
    if not isinstance(kernel_target, str) or not kernel_target:
        raise AssembleError("image manifest missing kernel_target")

    output_path.unlink(missing_ok=True)
    run_command(["dd", "if=/dev/zero", f"of={output_path}", "bs=1M", f"count={size_mib}", "status=none"])
    run_command(["mformat", "-i", str(output_path), "-F", "::"])

    for directory in ("/EFI", "/EFI/BOOT"):
        run_command(["mmd", "-i", str(output_path), f"::{directory}"])

    run_command(["mcopy", "-i", str(output_path), str(bootloader_path), f"::{efi_boot_path}"])
    run_command(["mcopy", "-i", str(output_path), str(kernel_path), f"::{kernel_target}"])
    with tempfile.NamedTemporaryFile(prefix="rucux-journal-", delete=False) as persistent_journal:
        persistent_journal.write(b"\0" * PERSISTENT_JOURNAL_SIZE)
        persistent_journal_path = Path(persistent_journal.name)
    try:
        run_command(["mcopy", "-i", str(output_path), str(persistent_journal_path), "::/journal.log"])
    finally:
        persistent_journal_path.unlink(missing_ok=True)

    product = data.get("product")
    if isinstance(product, dict):
        ports = product.get("ports", [])
        if isinstance(ports, list) and ports:
            try:
                run_command(["mmd", "-i", str(output_path), "::/packages"])
            except AssembleError:
                pass  # directory might already exist
            packages_dir = REPO_ROOT / "packages"
            for port in ports:
                for match in packages_dir.glob(f"{port}-*.rpkg"):
                    run_command(["mcopy", "-i", str(output_path), str(match), f"::/packages/{match.name}"])

    return output_path


def create_boot_scr(script_text: str, output_path: Path) -> None:
    data = script_text.encode("utf-8")
    data_crc = zlib.crc32(data) & 0xFFFFFFFF
    size = len(data)
    magic = 0x27051956
    timestamp = int(time.time())
    fmt = ">IIIIIIIBBBB32s"
    header_without_crc = struct.pack(fmt, magic, 0, timestamp, size, 0, 0, data_crc, 5, 2, 6, 0, b"rucux boot script")
    hcrc = zlib.crc32(header_without_crc) & 0xFFFFFFFF
    header = struct.pack(fmt, magic, hcrc, timestamp, size, 0, 0, data_crc, 5, 2, 6, 0, b"rucux boot script")
    output_path.write_bytes(header + data)


def assemble_raw_sd_image(data: dict[str, object], image: dict[str, object]) -> Path:
    image_size_mib = image.get("size_mib", 128)
    if not isinstance(image_size_mib, (str, int)):
        raise AssembleError("image.size_mib must be an integer")
    size_mib = int(image_size_mib)
    output_path = output_path_from_resolved(data, image)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    board = data.get("board")
    if not isinstance(board, dict):
        raise AssembleError("resolved board manifest is invalid")

    kernel_path = kernel_path_from_resolved(board)
    if not kernel_path.exists():
        raise AssembleError(f"kernel artifact not found: {kernel_path}")

    platform = board.get("platform")
    platform = platform if isinstance(platform, dict) else {}

    # Staging address the ELF is read into before `bootelf` relocates it to the
    # link address. It must be clear of both U-Boot and the kernel's own load
    # address, so it comes from the board manifest rather than a constant.
    stage_addr = platform.get("boot_stage_address", "0x10000000")

    with tempfile.TemporaryDirectory(prefix="rucux-sd-") as tmpdir:
        tmp_path = Path(tmpdir)
        boot_scr_path = tmp_path / "boot.scr"
        create_boot_scr(
            f"fatload mmc 0:1 {stage_addr} kernel.elf\nbootelf {stage_addr}\n",
            boot_scr_path,
        )

        fat_img_path = tmp_path / "fat.img"
        fat_size_mib = size_mib - 2
        run_command(["dd", "if=/dev/zero", f"of={fat_img_path}", "bs=1M", f"count={fat_size_mib}", "status=none"])
        run_command(["mformat", "-i", str(fat_img_path), "-F", "::"])

        run_command(["mcopy", "-i", str(fat_img_path), str(boot_scr_path), "::/boot.scr"])
        run_command(["mcopy", "-i", str(fat_img_path), str(kernel_path), "::/kernel.elf"])

        product = data.get("product")
        if isinstance(product, dict):
            ports = product.get("ports", [])
            if isinstance(ports, list) and ports:
                run_command(["mmd", "-i", str(fat_img_path), "::/packages"])
                packages_dir = REPO_ROOT / "packages"
                for port in ports:
                    for match in packages_dir.glob(f"{port}-*.rpkg"):
                        run_command(["mcopy", "-i", str(fat_img_path), str(match), f"::/packages/{match.name}"])

        output_path.unlink(missing_ok=True)
        run_command(["dd", "if=/dev/zero", f"of={output_path}", "bs=1M", f"count={size_mib}", "status=none"])

        lba_start = 2048
        num_sectors = (fat_size_mib * 1024 * 1024) // 512
        mbr = bytearray(512)
        mbr[446] = 0x80
        mbr[447:450] = b"\xfe\xff\xff"
        mbr[450] = 0x0C
        mbr[451:454] = b"\xfe\xff\xff"
        mbr[454:458] = lba_start.to_bytes(4, "little")
        mbr[458:462] = num_sectors.to_bytes(4, "little")
        mbr[510] = 0x55
        mbr[511] = 0xAA

        with open(output_path, "r+b") as out:
            out.seek(0)
            out.write(mbr)
            out.seek(lba_start * 512)
            with open(fat_img_path, "rb") as fat:
                shutil.copyfileobj(fat, out)

        write_boot_firmware(output_path, board, lba_start)

    return output_path


def write_boot_firmware(output_path: Path, board: dict[str, object], lba_start: int) -> None:
    """Write the SoC bootloader into the sectors ahead of the partition.

    Amlogic's boot ROM loads its first stage from raw sectors near the start of
    the card, before any partition table. Without this the image only boots on a
    board that already has firmware on eMMC.

    Offsets are vendor-specific and are read from the board manifest rather than
    hardcoded here; see .agents/runbooks/ODROID_C2_BRINGUP_ROADMAP.md.
    """
    boot = board.get("boot")
    boot = boot if isinstance(boot, dict) else {}

    firmware = boot.get("firmware_image")
    if not firmware:
        print(
            "warning: board declares no boot.firmware_image; the image has no "
            "bootloader and will only boot where firmware already exists on eMMC",
            file=sys.stderr,
        )
        return

    firmware_path = Path(str(firmware))
    if not firmware_path.is_absolute():
        firmware_path = REPO_ROOT / firmware_path
    if not firmware_path.exists():
        raise AssembleError(f"boot firmware image not found: {firmware_path}")

    try:
        offset = int(str(boot.get("firmware_offset_bytes", "512")), 0)
    except ValueError as exc:
        raise AssembleError(f"invalid boot.firmware_offset_bytes: {exc}") from exc

    blob = firmware_path.read_bytes()
    limit = lba_start * 512
    if offset + len(blob) > limit:
        raise AssembleError(
            f"firmware ({len(blob)} bytes at offset {offset}) would overrun the "
            f"partition start at byte {limit}; increase the partition offset"
        )

    with open(output_path, "r+b") as out:
        out.seek(offset)
        out.write(blob)

    print(f"wrote {len(blob)} bytes of boot firmware at offset {offset}")


def assemble_image(product_name: str, image_name: str) -> Path:
    data = product_info.resolved_image_manifest(product_name, image_name)
    image = data.get("image")
    if not isinstance(image, dict):
        raise AssembleError("resolved image manifest is invalid")

    kind = image.get("kind")
    if kind == "fat32-efi-disk":
        return assemble_uefi_disk(data, image)
    if kind == "raw-sd-image":
        return assemble_raw_sd_image(data, image)

    raise AssembleError(f"unsupported image kind: {kind}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Assemble rucux images from product manifests")
    parser.add_argument("product", help="Product name")
    parser.add_argument("image", help="Image name")
    args = parser.parse_args()

    for tool_name in ("dd", "mformat", "mmd", "mcopy"):
        if shutil.which(tool_name) is None:
            print(f"error: required host tool not found: {tool_name}", file=sys.stderr)
            return 1

    try:
        output_path = assemble_image(args.product, args.image)
    except (AssembleError, product_info.ManifestError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(output_path.relative_to(REPO_ROOT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
