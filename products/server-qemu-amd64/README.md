# Headless administration product

`server-qemu-amd64` is a single-CPU development target for the serial console and basic network diagnostics. It starts init, the console service, the PS/2 keyboard service, and the shell. Graphics, Wayland, logging daemons, and port libraries are not required.

Build from the repository root:

```sh
meson setup builddir-server --cross-file cross_amd64.txt -Dproduct=server-qemu-amd64
meson compile -C builddir-server
make -C bootloader ARCH=x86_64 TARGET=../builddir-server/BOOTX64.EFI
```

Run the offline integration test with local EDK2 firmware files:

```sh
python3 -B tools/test_console_smoke.py \
  --build-dir builddir-server \
  --bootloader builddir-server/BOOTX64.EFI \
  --uefi-code /path/to/edk2-x86_64-code.fd \
  --uefi-vars /path/to/edk2-i386-vars.fd
```

The test creates a 48 MiB FAT image and a writable firmware-variable copy inside the build directory. It checks serial input, CR-to-newline conversion, Delete/backspace and Ctrl-U editing, process status, timed sleep, SIMD state across thread switches, exit-status collection, service stop/start, repeated UDP loopback, and shell restart after Ctrl-D. `--isolated-nic` adds an E1000 attached only to an internal QEMU hub; it does not connect to a host or external network. Logs are written to `builddir-server/console-smoke.log`.

For an interactive session after generating the image:

```sh
qemu-system-x86_64 \
  -drive if=pflash,format=raw,readonly=on,file=/path/to/edk2-x86_64-code.fd \
  -drive if=pflash,format=raw,file=builddir-server/console-vars.fd \
  -drive if=ide,format=raw,file=builddir-server/console-smoke.img \
  -smp 1 -m 512M -serial stdio -monitor none -display none -nic none -no-reboot
```

Shell examples:

```text
help
ls /bin
uptime
top
service status console
service stop kbd
service start kbd
run /bin/netstat
run /bin/netcheck
run /bin/terminal_smoke
exit
```

`run` accepts an absolute executable path and waits for its exit status; argument passing is not implemented. `terminal_smoke` deliberately exits with status 7 after its checks. The serial shell returns after logout or Ctrl-D. Console status reflects init registration; it is not an independent service health probe.

## Current server limits

This is a trusted, isolated development environment, not a production or Internet-facing server. There is no login/authentication boundary or SSH service. PTYs, foreground job control, Ctrl-C signal delivery, shell pipelines/redirection, and complete raw-terminal timeout semantics remain unimplemented. The shell and keyboard share one TTY. Monotonic uptime is available; realtime clock/calendar functions are incomplete.

`netstat` reports the kernel's boot-time interface configuration. Loopback is `127.0.0.1`; an E1000, when present, uses the existing static `10.0.0.10/24` configuration with gateway `10.0.0.1`. These commands do not configure DHCP, routing, or DNS. The UDP test proves local socket/IPv4 delivery and repeated close/reopen; it does not prove external connectivity. NIC receive/IRQ integration, TCP teardown, malformed-packet handling, and resource limits need additional work before exposing a network service.

The next milestones are external networking in an explicitly isolated test network, persistent configuration/logging, and authenticated remote administration. Multi-CPU server operation and physical hardware remain outside this product's validated scope.
