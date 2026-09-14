# Tools

This directory is reserved for host-side helper tools that are larger or more product-oriented than the generic scripts in `scripts/`.

Examples:

- product assembly helpers
- release packaging tools
- manifest validation helpers
- board or product inspection tools

Keep small, generic helper scripts in `scripts/`. Use `tools/` when the helper becomes part of the workspace and distribution model.

## Python quality checks

Python 3.14+ and `uv` are required. Runtime scripts remain standard-library-only;
Ruff and mypy are development dependencies in `pyproject.toml`.

```sh
just py-sync       # Install the locked development/build/documentation tools
just py-check      # Ruff lint, mypy, and Ruff formatting check
just py-format     # Apply Python formatting
```

`just py-lint` and `just py-typecheck` run each check separately. `just check`
also includes `py-check` alongside the existing C++ checks. The Python checks
are read-only and run offline with `uv --no-sync`; run `py-sync` first to prepare
`.venv`. The sync command may download dependencies.

Both tools cover project scripts in `tools/` and `scripts/`. Generated outputs,
third-party ports, and agent tooling are outside this scope. Ruff checks Python
errors, imports, and modernization rules, and provides the formatter. Mypy checks
typed interfaces and the bodies of existing untyped functions; this is gradual
typing, not strict mode. Service policies and trace events use typed dictionaries,
and the Ethernet test peer has typed interfaces.

Tool caches are stored under `builddir-python-tools/`. No CI workflow is currently
configured; these checks run locally through `just`.
