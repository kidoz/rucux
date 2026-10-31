# Services

This directory is the long-term home for long-lived system services such as:

- `init`
- `traced`
- `console`
- `kbd`
- `net`

Each service has one authoritative `<service>/main.cpp` source. An optional `<service>/app.yaml` owns autostart, restart, and dependency policy; omitted fields default to disabled/zero and an empty dependency list. Product manifests select which services are built and embedded; `userspace/` contains only shared startup and linker support.
