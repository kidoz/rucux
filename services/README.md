# Services

This directory is the long-term home for long-lived system services such as:

- `init`
- `traced`
- `console`
- `kbd`
- `net`

Migration should happen in small Meson-safe slices. During the transition, some services will still live under `userspace/`.
