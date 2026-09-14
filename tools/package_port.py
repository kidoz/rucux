#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

import argparse
import os
import sys
import tarfile
from pathlib import Path

import product_info


def create_package(port_dir: Path, dest_dir: Path) -> int:
    port_yaml_path = port_dir / "port.yaml"
    if not port_yaml_path.exists():
        print(f"error: {port_yaml_path} not found", file=sys.stderr)
        return 1

    try:
        manifest = product_info.parse_simple_yaml(port_yaml_path)
    except product_info.ManifestError as e:
        print(f"error parsing {port_yaml_path}: {e}", file=sys.stderr)
        return 1

    name = manifest.get("name")
    version = manifest.get("version")

    if not name or not version:
        print("error: port.yaml must contain 'name' and 'version'", file=sys.stderr)
        return 1

    staging_dir = port_dir / "pkg-stage"

    # Write the arch into the manifest and determine filename
    arch = os.environ.get("RUCUX_ARCH", "any")
    manifest["arch"] = arch

    # Save the updated manifest back to staging
    staging_yaml = staging_dir / "port.yaml"
    staging_yaml.parent.mkdir(parents=True, exist_ok=True)
    with open(staging_yaml, "w") as f:
        for k, v in manifest.items():
            if isinstance(v, list):
                f.write(f"{k}:\n")
                for item in v:
                    f.write(f"  - {item}\n")
            else:
                f.write(f"{k}: {v}\n")

    pkg_name = f"{name}-{version}-{arch}.rpkg"
    dest_path = dest_dir / pkg_name

    if not staging_dir.exists() or not staging_dir.is_dir():
        print(f"error: staging directory {staging_dir} does not exist", file=sys.stderr)
        return 1

    print(f"Packaging {name} {version} into {pkg_name}...")

    # Create the tarball (uncompressed for now to simplify native pkg tool)
    with tarfile.open(dest_path, "w") as tar:
        # Add the contents of the staging directory under 'sysroot' root
        for item in staging_dir.iterdir():
            tar.add(item, arcname=item.name)

    print(f"Successfully created {dest_path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Package a rucux port into a .rpkg file")
    parser.add_argument("port_dir", help="Path to the port directory containing port.yaml and pkg-stage/")
    parser.add_argument(
        "--dest", default=str(product_info.REPO_ROOT / "packages"), help="Destination directory for the .rpkg file"
    )

    args = parser.parse_args()

    port_dir = Path(args.port_dir).resolve()
    dest_dir = Path(args.dest).resolve()

    dest_dir.mkdir(parents=True, exist_ok=True)

    return create_package(port_dir, dest_dir)


if __name__ == "__main__":
    sys.exit(main())
