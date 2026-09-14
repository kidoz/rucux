# Headless administration product

`server-qemu-amd64` is a single-CPU development target for the serial console and basic UDP/TCP services. It starts init, the console service, the PS/2 keyboard service, and the shell. Graphics, Wayland, logging daemons, and port libraries are not required.

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

Add `--network-peer` instead of `--isolated-nic` to exercise actual Ethernet traffic. The test passes frames between QEMU and a deterministic Python peer over an inherited Unix socket pair. It uses no host IP socket, TAP interface, bridge, or external network. It verifies ARP, malformed IPv4/UDP/TCP rejection, UDP payloads from 0 to 1472 bytes, 32 TCP connections carrying 2048 bytes each, FIN/ACK teardown, reconnection using the same peer port, and restarting the service. It also verifies delivery of the first UDP packet after a lost ARP request and deliberately loses TCP SYN/ACK, handshake ACK, data, data ACK, FIN, and final ACK packets to exercise retransmission. It negotiates a 256-byte MSS, delivers TCP segments out of order and retransmits the missing suffix, then transfers 64 KiB over a connection held at a zero receive window for six backoff probes. A lost window update is recovered by a probe; subsequent sends respect the reopened 128-byte window. Serial commands execute during traffic and while the reader is stalled. The packet capture is `builddir-server/network-peer.pcap`.

The real NIC path is E1000 PCI INTx receive interrupts and descriptor DMA. The firmware/PIC route is exercised by the smoke test; the IOAPIC route is build-checked only.

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
start /bin/netecho
run /bin/terminal_smoke
exit
```

`run` accepts an absolute executable path and waits for its exit status. `start` launches a background program and returns its PID; the shell reaps finished children between commands. Neither command supports arguments or job control. Background programs inherit the console, so only programs that do not read terminal input should be started this way. `terminal_smoke` deliberately exits with status 7 after its checks. The serial shell returns after logout or Ctrl-D. Console status reflects init registration; it is not an independent service health probe.

`netecho` is a diagnostic echo service on UDP port 19091 and TCP port 19092. TCP connections are served sequentially; a peer sends data, half-closes its stream, reads the echo, then closes. A UDP datagram containing `quit` shuts down the service. It also exits after 30 seconds without service activity, readable client data, or client write readiness. It has no authentication and is intended only for the isolated test.

Hosted regression checks:

```sh
make -C tests run
c++ -std=c++23 -g -fsanitize=address,undefined \
  -Isrc/include -idirafter lib/include -I. tests/test_network.cpp \
  src/kernel/net/{netbuf,netif,ethernet,arp,ipv4,checksum,udp,tcp,socket}.cpp \
  -o builddir-host-tests/test_network_sanitized
./builddir-host-tests/test_network_sanitized
```

The network suite uses production protocol/socket code with host allocation, scheduler and I/O boundaries. It checks length/checksum validation, bounded queues/backlogs, receive-buffer saturation, file-descriptor exhaustion, partial acknowledgements, congestion backpressure, RTT estimation and ambiguous ACK suppression, MSS option validation, sequence wraparound, zero-window recovery and silence expiry, retry exhaustion, simultaneous retransmissions, ARP queue expiry, timeout cleanup, and 100 TCP lifecycles with allocation counts and bytes returning to baseline. A zero-window peer answers 100 probes over several simulated minutes without increasing live allocations or bytes. These host checks do not establish DMA or SMP correctness.

## Current server limits

This is a trusted, isolated development environment, not a production or Internet-facing server. There is no login/authentication boundary or SSH service. PTYs, foreground job control, Ctrl-C signal delivery, shell pipelines/redirection, and complete raw-terminal timeout semantics remain unimplemented. The shell and keyboard share one TTY. Monotonic uptime is available; realtime clock/calendar functions are incomplete.

`netstat` reports the kernel's boot-time interface configuration. Loopback is `127.0.0.1`; an E1000, when present, uses the existing static `10.0.0.10/24` configuration with gateway `10.0.0.1`. These commands do not configure DHCP, routing, or DNS. The isolated peer test proves local Ethernet UDP/TCP delivery beyond loopback; it does not establish Internet interoperability.

UDP sends are limited to 1472 bytes and each receive queue to 32 packets. TCP is limited to 128 control blocks, at most 16 pending connections per listener, and 32767 receive bytes per connection. Half-open connections and closed connections have a 15-second lifetime limit; established but unaccepted connections expire after 5 seconds. The development TIME_WAIT interval is 2 seconds. TCP retains at most eight unacknowledged data segments per connection plus one FIN. Sends may return short counts or EAGAIN when the peer window, congestion window, or this queue is exhausted. A deferred FIN waits for space in both send windows and remains subject to the 15-second close deadline.

TCP advertises an MSS of 1460 bytes and caps sends to the peer's MSS (536 bytes when absent). Malformed option lengths, duplicate MSS options, and zero MSS values are rejected. The congestion window begins at one MSS, grows through slow start and additive congestion avoidance, and is capped at eight MSS. A timeout reduces it to one MSS and reduces the slow-start threshold; an idle sender also restarts at one MSS.

TCP estimates RTT and its variation with integer arithmetic, excluding ambiguous acknowledgements after retransmission. The retransmission timeout starts at one second, stays within 1–60 seconds, and doubles after a timeout. Four retransmission attempts are allowed; exhaustion reports ETIMEDOUT and releases retained packets. Cumulative and partial acknowledgements remove acknowledged data. A retransmission respects the current peer window and MSS.

A zero peer window suspends loss retries. Empty ACK probes use the old sequence number without advancing the stream or allocating retained packets. Probe intervals back off from 1 to 2 to 4 seconds; replies keep the connection alive, while 15 seconds without a valid window response reports ETIMEDOUT. Reopening the window resumes retained data and write readiness. Existing close and unaccepted-connection deadlines still apply. This is a bounded development policy, not a claim of complete Internet TCP conformance. Fast retransmit/recovery, SACK, out-of-order buffering, window scaling, and complete socket option/nonblocking semantics remain unimplemented; reordered data is dropped and cumulatively acknowledged for peer retransmission.

ARP queues at most four packets for each of eight unresolved neighbors, sends up to three requests one second apart, and discards unresolved queues after three seconds. Cache entries are scoped to the interface; dynamically learned entries expire after 60 seconds. Queue/ring exhaustion drops packets. TCP can recover a dropped frame within its retry budget; UDP remains best effort. ICMP, fragmentation/reassembly, DHCP and DNS are unavailable. Testing covers a 1500-byte-MTU Ethernet link with deterministic packet loss; it does not establish general Internet TCP compatibility. Concurrent close/read on shared sockets and socket operations across multiple CPUs remain unvalidated.

The next milestones are interoperability with an independent TCP stack, persistent configuration/logging, and authenticated remote administration. Multi-CPU server operation and physical hardware remain outside this product's validated scope.
