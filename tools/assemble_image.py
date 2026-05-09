#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
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
    return output_path


def assemble_image(product_name: str, image_name: str) -> Path:
    data = product_info.resolved_image_manifest(product_name, image_name)
    image = data.get("image")
    if not isinstance(image, dict):
        raise AssembleError("resolved image manifest is invalid")

    kind = image.get("kind")
    if kind == "fat32-efi-disk":
        return assemble_uefi_disk(data, image)

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
