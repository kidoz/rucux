# Programs

This directory is the long-term home for user-facing programs and utilities such as:

- `sh`
- `trace`
- `ls`
- `cat`
- `echo`
- `top`
- `rtorrent`

Each program has one authoritative `<program>/main.cpp` source. Programs managed by `init` can declare autostart, restart, and dependency policy in `<program>/app.yaml`; omitted fields default to disabled/zero and an empty dependency list. Product manifests select which programs are built and embedded; `userspace/` contains only shared startup and linker support.
