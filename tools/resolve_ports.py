#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import sys

import product_info


PORT_DEPENDENCIES = {
    "openlibm": [],
    "zlib": [],
    "libcxx": ["openlibm"],
    "libressl": ["zlib"],
    "ncurses": [],
    "curl": ["zlib", "libressl"],
    "libtorrent": ["libcxx", "libressl"],
    "rtorrent": ["libtorrent", "curl", "ncurses"],
    "libffi": [],
    "expat": [],
    "vulkan-headers": [],
    "wayland": ["libffi", "expat"],
    "libdrm": [],
    "llvm": ["libcxx"],
    "mesa": ["zlib", "vulkan-headers", "libdrm", "wayland", "libcxx", "llvm"],
}


def normalize_ports(product_name: str) -> list[str]:
    data = product_info.resolved_manifest(product_name)
    product = data.get("product")
    if not isinstance(product, dict):
        raise RuntimeError("invalid product manifest")

    ports = product.get("ports", [])
    if not isinstance(ports, list):
        return []
    return [str(port) for port in ports]


def resolve_ports(requested_ports: list[str]) -> list[str]:
    resolved: list[str] = []
    seen: set[str] = set()
    visiting: set[str] = set()

    def visit(port_name: str) -> None:
        if port_name in seen:
            return
        if port_name in visiting:
            raise RuntimeError(f"cyclic port dependency at '{port_name}'")
        if port_name not in PORT_DEPENDENCIES:
            raise RuntimeError(f"unknown port '{port_name}'")

        visiting.add(port_name)
        for dependency in PORT_DEPENDENCIES[port_name]:
            visit(dependency)
        visiting.remove(port_name)

        seen.add(port_name)
        resolved.append(port_name)

    for port in requested_ports:
        visit(port)

    return resolved


def main() -> int:
    parser = argparse.ArgumentParser(description="Resolve product port order for rucux")
    parser.add_argument("product", help="Product name")
    parser.add_argument("--csv", action="store_true", help="Print as a CSV list")
    args = parser.parse_args()

    try:
        ordered = resolve_ports(normalize_ports(args.product))
    except Exception as error:  # noqa: BLE001
        print(f"error: {error}", file=sys.stderr)
        return 1

    if args.csv:
        print(",".join(ordered))
    else:
        for port in ordered:
            print(port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
