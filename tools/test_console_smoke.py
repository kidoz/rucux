#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build an offline UEFI test image and verify the serial administration console."""

import argparse
import shutil
import socket
import subprocess
import time
from pathlib import Path

import network_peer


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--bootloader", type=Path, required=True)
    parser.add_argument("--uefi-code", type=Path, required=True)
    parser.add_argument("--uefi-vars", type=Path, required=True)
    parser.add_argument(
        "--isolated-nic", action="store_true", help="Attach an E1000 to an isolated QEMU hub; no host network"
    )
    parser.add_argument(
        "--network-peer", action="store_true", help="Exercise an Ethernet peer over an inherited local socket pair"
    )
    args = parser.parse_args()
    if args.network_peer and args.isolated_nic:
        parser.error("choose only one NIC test mode")
    root: Path = args.build_dir.resolve()
    repo = Path(__file__).resolve().parent.parent
    if not root.is_relative_to(repo) or not (root / "kernel.elf").is_file():
        parser.error("build-dir must contain kernel.elf inside the repository")
    image = root / "console-smoke.img"
    with image.open("wb") as f:
        f.truncate(48 * 1024 * 1024)
    for cmd in [
        ["mformat", "-i", str(image), "-F", "::"],
        ["mmd", "-i", str(image), "::/EFI", "::/EFI/BOOT"],
        ["mcopy", "-i", str(image), str(args.bootloader.resolve()), "::/EFI/BOOT/BOOTX64.EFI"],
        ["mcopy", "-i", str(image), str(root / "kernel.elf"), "::/kernel.elf"],
    ]:
        subprocess.run(cmd, check=True)
    variables = root / "console-vars.fd"
    shutil.copyfile(args.uefi_vars, variables)
    command = [
        "qemu-system-x86_64",
        "-drive",
        f"if=pflash,format=raw,readonly=on,file={args.uefi_code.resolve()}",
        "-drive",
        f"if=pflash,format=raw,file={variables}",
        "-drive",
        f"file={image},if=ide,format=raw",
        "-smp",
        "1",
        "-m",
        "512M",
        "-serial",
        "stdio",
        "-monitor",
        "none",
        "-display",
        "none",
        "-nic",
        "none",
        "-no-reboot",
    ]
    if args.isolated_nic:
        command += ["-netdev", "hubport,id=isolated,hubid=0", "-device", "e1000,netdev=isolated"]
    peer_socket = child_socket = capture = None
    if args.network_peer:
        peer_socket, child_socket = socket.socketpair()
        command += [
            "-netdev",
            f"socket,id=peer,fd={child_socket.fileno()}",
            "-device",
            "e1000,netdev=peer,mac=52:54:00:12:34:56",
        ]
        capture = (root / "network-peer.pcap").open("wb")
    log_path = root / "console-smoke.log"
    with log_path.open("wb") as log:
        process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=log,
            stderr=subprocess.STDOUT,
            pass_fds=(child_socket.fileno(),) if child_socket else (),
        )
        if child_socket:
            child_socket.close()

        def wait_for(marker: str, start: int = 0, seconds: float = 12) -> str:
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                text = log_path.read_text(errors="replace")
                if any(s in text for s in ["EXCEPTION ", "#PF at ", "Fault RIP=", "PANIC"]):
                    raise RuntimeError("guest kernel fault")
                if marker in text[start:]:
                    return text
                if process.poll() is not None:
                    raise RuntimeError(f"QEMU exited with {process.returncode}")
                time.sleep(0.05)
            raise RuntimeError(f"timeout waiting for {marker!r}")

        def check(data: str, marker: str) -> None:
            assert process.stdin is not None
            start = len(log_path.read_text(errors="replace"))
            for byte in data.encode():
                process.stdin.write(bytes([byte]))
                process.stdin.flush()
                time.sleep(0.02)
            wait_for(marker, start)

        try:
            wait_for("$ ")
            time.sleep(2)  # Must still accept input after all startup work sleeps.
            check("echo serialok\r", "\nserialok\n")
            check("echo bad\x15echo erasedok\r", "\nerasedok\n")
            check("echo editx\x7fok\r", "\neditok\n")
            check("help\r", "Built-in commands:")
            check("service status console\r", "console: RUNNING")
            check("service status sh\r", "sh: RUNNING")
            check("ls /bin\r", "console  ")
            check("top\r", "Total threads:")
            check("uptime\r", "uptime: ")
            check("run /bin/terminal_smoke\r", "run: exit status 7")
            check("run /bin/netstat\r", "lo: address=127.0.0.1")
            if args.isolated_nic:
                check("run /bin/netstat\r", "eth0: address=10.0.0.10")
            check("run /bin/netcheck\r", "netcheck: UDP loopback PASS")
            check("service stop kbd\r", "init: reaped kbd")
            check("service status kbd\r", "kbd: STOPPED")
            check("service start kbd\r", "init: registered service kbd")
            check("service status kbd\r", "kbd: RUNNING")
            check("echo afterrestart\r", "\nafterrestart\n")
            check("run /bin/netcheck\r", "netcheck: UDP loopback PASS")
            check("\x04", "Welcome to rucux sh")
            check("echo newlogin\r", "\nnewlogin\n")
            if args.network_peer:
                assert peer_socket is not None and capture is not None
                network_peer.verify(network_peer.Peer(peer_socket, capture), check, wait_for)
                print(
                    "PASS: isolated Ethernet ARP/UDP, TCP retransmission/reconnect, malformed packets, concurrent console"
                )
            print(f"PASS: serial console, editing, service queries, directory listing, process status; {log_path}")
        except Exception as error:
            print(f"FAIL: {error}; {log_path}")
            return 1
        finally:
            if peer_socket:
                peer_socket.close()
            if capture:
                capture.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
