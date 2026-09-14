#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

from __future__ import annotations

import argparse
import sys

import product_info


def load_port_dependencies() -> dict[str, list[str]]:
    ports_dir = product_info.REPO_ROOT / "ports"
    deps = {}
    for port_dir in ports_dir.iterdir():
        if not port_dir.is_dir():
            continue
        port_yaml = port_dir / "port.yaml"
        if not port_yaml.exists():
            continue

        try:
            data = product_info.parse_simple_yaml(port_yaml)
            name = str(data.get("name", port_dir.name))
            dependencies = data.get("dependencies", [])
            if not isinstance(dependencies, list):
                dependencies = []
            deps[name] = [str(d) for d in dependencies]
        except Exception as e:
            print(f"warning: failed to parse {port_yaml}: {e}", file=sys.stderr)
    return deps


PORT_DEPENDENCIES = load_port_dependencies()


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
