# Release

This directory now contains release definitions.

Each release definition should pin:

- one product
- one version/channel identity
- the image set to materialize
- any release-specific overrides

Concrete generated release outputs belong under `builddir/release/<release>/`.

Current workflow:

- define `release/<name>/release.yaml`
- run `python3 tools/generate_release_manifest.py <name>`
- or use the `just release-manifest <name>` wrapper
