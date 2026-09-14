#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PRODUCTS_DIR = REPO_ROOT / "products"
BOARDS_DIR = REPO_ROOT / "boards"
IMAGES_DIR = REPO_ROOT / "images"


class ManifestError(RuntimeError):
    pass


def parse_scalar(value: str) -> str | list[str]:
    value = value.strip()
    if value == "[]":
        return []
    if (value.startswith('"') and value.endswith('"')) or (value.startswith("'") and value.endswith("'")):
        return value[1:-1]
    return value


def parse_simple_yaml(path: Path) -> dict[str, object]:
    lines: list[tuple[int, str]] = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        stripped = raw_line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        indent = len(raw_line) - len(raw_line.lstrip(" "))
        lines.append((indent, stripped))

    root: dict[str, object] = {}
    stack: list[tuple[int, object]] = [(-1, root)]

    for index, (indent, stripped) in enumerate(lines):
        while len(stack) > 1 and indent <= stack[-1][0]:
            stack.pop()

        parent = stack[-1][1]

        if stripped.startswith("- "):
            if not isinstance(parent, list):
                raise ManifestError(f"{path}: list item without list parent: {stripped}")
            parent.append(parse_scalar(stripped[2:]))
            continue

        if ":" not in stripped:
            raise ManifestError(f"{path}: invalid line: {stripped}")

        key, raw_value = stripped.split(":", 1)
        key = key.strip()
        value = raw_value.strip()

        if not isinstance(parent, dict):
            raise ManifestError(f"{path}: mapping entry without mapping parent: {stripped}")

        if value:
            parent[key] = parse_scalar(value)
            continue

        next_container: object = {}
        if index + 1 < len(lines):
            next_indent, next_stripped = lines[index + 1]
            if next_indent > indent and next_stripped.startswith("- "):
                next_container = []

        parent[key] = next_container
        stack.append((indent, next_container))

    return root


def get_nested(data: object, dotted_key: str) -> object:
    current = data
    for segment in dotted_key.split("."):
        if not isinstance(current, dict) or segment not in current:
            raise ManifestError(f"unknown key: {dotted_key}")
        current = current[segment]
    return current


def load_product(product_name: str) -> tuple[dict[str, object], dict[str, object]]:
    product_path = PRODUCTS_DIR / product_name / "product.yaml"
    if not product_path.exists():
        raise ManifestError(f"product manifest not found: {product_path}")

    product = parse_simple_yaml(product_path)
    board_name = product.get("board")
    if not isinstance(board_name, str) or not board_name:
        raise ManifestError(f"{product_path}: missing board")

    board_path = BOARDS_DIR / board_name / "board.yaml"
    if not board_path.exists():
        raise ManifestError(f"board manifest not found: {board_path}")

    board = parse_simple_yaml(board_path)
    return product, board


def load_image(image_name: str) -> dict[str, object]:
    image_path = IMAGES_DIR / image_name / "image.yaml"
    if not image_path.exists():
        raise ManifestError(f"image manifest not found: {image_path}")
    return parse_simple_yaml(image_path)


def resolved_manifest(product_name: str) -> dict[str, object]:
    product, board = load_product(product_name)
    return {
        "product": product,
        "board": board,
        "name": product.get("name", product_name),
        "board_name": board.get("name", product.get("board", "")),
        "arch": board.get("arch", ""),
    }


def resolved_image_manifest(product_name: str, image_name: str) -> dict[str, object]:
    data = resolved_manifest(product_name)
    product = data["product"]
    if not isinstance(product, dict):
        raise ManifestError("resolved product manifest is invalid")

    product_images = product.get("images", [])
    if not isinstance(product_images, list) or image_name not in product_images:
        raise ManifestError(f"product '{product_name}' does not declare image '{image_name}'")

    image = load_image(image_name)
    return {
        **data,
        "image": image,
        "image_name": image.get("name", image_name),
    }


def format_value(value: object) -> str:
    if isinstance(value, list):
        return ",".join(str(item) for item in value)
    if isinstance(value, dict):
        return ",".join(f"{key}={value[key]}" for key in sorted(value))
    return str(value)


def command_list_products() -> int:
    for product_path in sorted(PRODUCTS_DIR.glob("*/product.yaml")):
        print(product_path.parent.name)
    return 0


def command_summary(product_name: str) -> int:
    data = resolved_manifest(product_name)
    product = data["product"]
    board = data["board"]
    if not isinstance(product, dict):
        raise ManifestError("resolved product manifest is invalid")

    print(f"product: {data['name']}")
    print(f"board: {data['board_name']}")
    print(f"arch: {data['arch']}")
    print(f"boot.firmware: {format_value(get_nested(board, 'boot.firmware'))}")
    print(f"boot.bootloader: {format_value(get_nested(board, 'boot.bootloader'))}")
    print(f"runtime: {format_value(get_nested(board, 'runtime'))}")
    print(f"artifacts: {format_value(get_nested(board, 'artifacts'))}")
    if "services" in product:
        print(f"services: {format_value(product['services'])}")
    if "programs" in product:
        print(f"programs: {format_value(product['programs'])}")
    if "ports" in product:
        print(f"ports: {format_value(product['ports'])}")
    if "images" in product:
        print(f"images: {format_value(product['images'])}")
    return 0


def command_get(product_name: str, key: str) -> int:
    data = resolved_manifest(product_name)
    value = get_nested(data, key)
    print(format_value(value))
    return 0


def command_image_summary(product_name: str, image_name: str) -> int:
    data = resolved_image_manifest(product_name, image_name)
    image = data["image"]

    print(f"product: {data['name']}")
    print(f"board: {data['board_name']}")
    print(f"image: {data['image_name']}")
    print(f"image.kind: {format_value(get_nested(image, 'kind'))}")
    if isinstance(image, dict):
        for key in sorted(image):
            if key in ("name", "kind"):
                continue
            print(f"image.{key}: {format_value(image[key])}")
    return 0


def command_get_image(product_name: str, image_name: str, key: str) -> int:
    data = resolved_image_manifest(product_name, image_name)
    value = get_nested(data, key)
    print(format_value(value))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Resolve rucux board and product manifests")
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("list-products", help="List available products")

    summary_parser = subparsers.add_parser("summary", help="Show resolved product summary")
    summary_parser.add_argument("product", help="Product name")

    get_parser = subparsers.add_parser("get", help="Read one resolved manifest key")
    get_parser.add_argument("product", help="Product name")
    get_parser.add_argument("key", help="Resolved key, for example board.name or board.artifacts.kernel")

    image_summary_parser = subparsers.add_parser("image-summary", help="Show resolved image summary")
    image_summary_parser.add_argument("product", help="Product name")
    image_summary_parser.add_argument("image", help="Image name")

    image_get_parser = subparsers.add_parser("get-image", help="Read one resolved image key")
    image_get_parser.add_argument("product", help="Product name")
    image_get_parser.add_argument("image", help="Image name")
    image_get_parser.add_argument("key", help="Resolved key, for example image.kind or board.artifacts.uefi_image")

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        if args.command == "list-products":
            return command_list_products()
        if args.command == "summary":
            return command_summary(args.product)
        if args.command == "get":
            return command_get(args.product, args.key)
        if args.command == "image-summary":
            return command_image_summary(args.product, args.image)
        if args.command == "get-image":
            return command_get_image(args.product, args.image, args.key)
        parser.error(f"unknown command: {args.command}")
    except ManifestError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
