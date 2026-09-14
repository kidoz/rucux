#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

import assemble_image
import product_info
import resolve_ports

REPO_ROOT = Path(__file__).resolve().parent.parent
RELEASES_DIR = REPO_ROOT / "release"


class ReleaseError(RuntimeError):
    pass


def load_release_definition(release_name: str) -> dict[str, object]:
    release_path = RELEASES_DIR / release_name / "release.yaml"
    if not release_path.exists():
        raise ReleaseError(f"release manifest not found: {release_path}")
    data = product_info.parse_simple_yaml(release_path)
    if not isinstance(data, dict):
        raise ReleaseError(f"invalid release manifest: {release_path}")
    return data


def normalize_list(value: object) -> list[str]:
    if isinstance(value, list):
        return [str(item) for item in value]
    return []


def require_string(data: dict[str, object], key: str) -> str:
    value = data.get(key)
    if not isinstance(value, str) or not value:
        raise ReleaseError(f"release manifest missing {key}")
    return value


def image_output_path(product_name: str, image_name: str) -> Path:
    resolved = product_info.resolved_image_manifest(product_name, image_name)
    image = resolved.get("image")
    if not isinstance(image, dict):
        raise ReleaseError(f"invalid image manifest for {image_name}")
    return assemble_image.output_path_from_resolved(resolved, image)


def image_kind(product_name: str, image_name: str) -> str:
    resolved = product_info.resolved_image_manifest(product_name, image_name)
    image = resolved.get("image")
    if not isinstance(image, dict):
        raise ReleaseError(f"invalid image manifest for {image_name}")
    kind = image.get("kind")
    if not isinstance(kind, str) or not kind:
        raise ReleaseError(f"image manifest missing kind for {image_name}")
    return kind


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def copy_artifact(src: Path, dst: Path) -> None:
    ensure_parent(dst)
    shutil.copy2(src, dst)


def maybe_materialize_image(product_name: str, image_name: str, output_dir: Path) -> dict[str, object]:
    kind = image_kind(product_name, image_name)
    entry: dict[str, object] = {
        "name": image_name,
        "kind": kind,
    }

    if kind == "fat32-efi-disk":
        assembled = assemble_image.assemble_image(product_name, image_name)
        destination = output_dir / "images" / assembled.name
        copy_artifact(assembled, destination)
        entry["materialized"] = "true"
        entry["source"] = str(assembled.relative_to(REPO_ROOT))
        entry["artifact"] = str(destination.relative_to(REPO_ROOT))
        return entry

    entry["materialized"] = "false"
    output_path = image_output_path(product_name, image_name)
    entry["source"] = str(output_path.relative_to(REPO_ROOT))
    return entry


def write_release_manifest(
    output_path: Path,
    release_name: str,
    release_def: dict[str, object],
    product_name: str,
    product_data: dict[str, object],
    release_images: list[str],
    resolved_ports: list[str],
    materialized_images: list[dict[str, object]],
) -> None:
    board_name = str(product_data["board_name"])
    arch = str(product_data["arch"])
    version = require_string(release_def, "version")
    channel = require_string(release_def, "channel")

    product = product_data.get("product")
    if not isinstance(product, dict):
        raise ReleaseError("invalid resolved product data")

    services = normalize_list(product.get("services"))
    programs = normalize_list(product.get("programs"))

    with output_path.open("w", encoding="utf-8") as out:
        out.write("name: " + release_name + "\n")
        out.write("version: " + version + "\n")
        out.write("channel: " + channel + "\n")
        out.write("product: " + product_name + "\n")
        out.write("board: " + board_name + "\n")
        out.write("arch: " + arch + "\n")
        out.write("services:\n")
        for service in services:
            out.write(f"  - {service}\n")
        out.write("programs:\n")
        for program in programs:
            out.write(f"  - {program}\n")
        out.write("ports:\n")
        for port in resolved_ports:
            out.write(f"  - {port}\n")
        out.write("images:\n")
        for image_name in release_images:
            out.write(f"  - {image_name}\n")
        out.write("artifacts:\n")
        for image in materialized_images:
            out.write(f"  - name: {image['name']}\n")
            out.write(f"    kind: {image['kind']}\n")
            out.write(f"    materialized: {image['materialized']}\n")
            out.write(f"    source: {image['source']}\n")
            if "artifact" in image:
                out.write(f"    artifact: {image['artifact']}\n")


def generate_release(release_name: str) -> Path:
    release_def = load_release_definition(release_name)
    product_name = require_string(release_def, "product")
    product_data = product_info.resolved_manifest(product_name)
    release_images = normalize_list(release_def.get("images"))
    if not release_images:
        product = product_data.get("product")
        if not isinstance(product, dict):
            raise ReleaseError("invalid resolved product data")
        release_images = normalize_list(product.get("images"))

    product = product_data.get("product")
    if not isinstance(product, dict):
        raise ReleaseError("invalid resolved product data")

    declared_images = normalize_list(product.get("images"))
    for image_name in release_images:
        if image_name not in declared_images:
            raise ReleaseError(f"release image '{image_name}' is not declared by product '{product_name}'")

    release_output_dir = REPO_ROOT / "builddir" / "release" / release_name
    release_output_dir.mkdir(parents=True, exist_ok=True)

    resolved_ports = resolve_ports.resolve_ports(resolve_ports.normalize_ports(product_name))
    materialized_images = [
        maybe_materialize_image(product_name, image_name, release_output_dir) for image_name in release_images
    ]

    manifest_path = release_output_dir / "manifest.yaml"
    write_release_manifest(
        manifest_path,
        release_name,
        release_def,
        product_name,
        product_data,
        release_images,
        resolved_ports,
        materialized_images,
    )
    return manifest_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a concrete release manifest for rucux")
    parser.add_argument("release", help="Release name")
    args = parser.parse_args()

    try:
        manifest_path = generate_release(args.release)
    except (ReleaseError, product_info.ManifestError, assemble_image.AssembleError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(manifest_path.relative_to(REPO_ROOT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
