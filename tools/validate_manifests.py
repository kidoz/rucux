#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

import assemble_image
import generate_product_headers
import product_info

REPO_ROOT = Path(__file__).resolve().parent.parent


def validate_product(product_path: Path, output_dir: Path) -> int:
    product_name = product_path.parent.name
    data = product_info.resolved_manifest(product_name)
    if data["name"] != product_name:
        raise RuntimeError(f"{product_path}: name must match its directory")

    services, programs, apps = generate_product_headers.resolve_product(product_name)
    for app in apps:
        kind = "services" if app in services else "programs"
        source = REPO_ROOT / kind / app / "main.cpp"
        if not source.is_file():
            raise RuntimeError(f"{product_path}: missing source for {app}: {source.relative_to(REPO_ROOT)}")

    generate_product_headers.write_embedded_header(output_dir / f"{product_name}-embedded.hpp", apps)
    generate_product_headers.write_init_config_header(output_dir / f"{product_name}-init.hpp", services, programs)

    product = data["product"]
    if not isinstance(product, dict):
        raise RuntimeError(f"{product_path}: invalid product manifest")
    images = product.get("images", [])
    if not isinstance(images, list):
        raise RuntimeError(f"{product_path}: images must be a list")
    image_count = 0
    for image_name in images:
        resolved = product_info.resolved_image_manifest(product_name, str(image_name))
        image = resolved["image"]
        if not isinstance(image, dict):
            raise RuntimeError(f"{product_path}: invalid image manifest for {image_name}")
        if image.get("kind") in ("fat32-efi-disk", "raw-sd-image"):
            assemble_image.output_path_from_resolved(resolved, image)
            image_count += 1

    print(f"{product_name}: {len(apps)} applications, {image_count} assemblable images")
    return len(apps)


def main() -> int:
    product_paths = sorted(product_info.PRODUCTS_DIR.glob("*/product.yaml"))
    if not product_paths:
        print("error: no product manifests found", file=sys.stderr)
        return 1

    try:
        output_root = REPO_ROOT / "build"
        output_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="manifest-validation-", dir=output_root) as tmp:
            output_dir = Path(tmp)
            application_count = sum(validate_product(path, output_dir) for path in product_paths)
    except (RuntimeError, product_info.ManifestError, assemble_image.AssembleError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"manifest validation: PASS ({len(product_paths)} products, {application_count} application selections)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
