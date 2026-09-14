# SPDX-License-Identifier: MIT
"""Exercise the guest's TCP server through restricted QEMU localhost forwarding.

The host socket API and libslirp provide independent TCP implementations. This
module never connects anywhere except 127.0.0.1. Run only with network approval.
"""

import select
import socket
import struct
import time
from collections import Counter
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from network_peer import GUEST_IP, WaitFor, checksum
from udp_forwarder import RELAY_IP


def choose_port() -> int:
    # Reserve neither port beyond this check: QEMU must bind them itself. A race
    # is reported as a QEMU startup failure, never retried against another service.
    for _ in range(10):
        with socket.socket() as tcp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            tcp.bind(("127.0.0.1", 0))
            port = tcp.getsockname()[1]
            try:
                udp.bind(("127.0.0.1", port))
            except OSError:
                continue
            return int(port)
    raise RuntimeError("unable to find a free localhost TCP/UDP port")


def transfer(port: int, size: int, delay_read: float = 0) -> None:
    payload = bytes(i % 251 for i in range(size))
    with socket.socket() as client:
        client.settimeout(45)
        if delay_read:
            client.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        client.connect(("127.0.0.1", port))

        def send() -> None:
            client.sendall(payload)
            client.shutdown(socket.SHUT_WR)

        # Read and write concurrently to avoid an application-level deadlock
        # when both independent stacks have filled their stream buffers.
        with ThreadPoolExecutor(max_workers=1) as writer:
            sent = writer.submit(send)
            try:
                time.sleep(delay_read)
                received = 0
                deadline = time.monotonic() + 90
                while True:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0 or not select.select([client], [], [], remaining)[0]:
                        raise TimeoutError(f"transfer deadline after {received}/{size} bytes")
                    chunk = client.recv(65536)
                    if not chunk:
                        break
                    if chunk != payload[received : received + len(chunk)]:
                        raise AssertionError(f"stream mismatch after {received}/{size} bytes")
                    received += len(chunk)
                assert received == size, f"early EOF after {received}/{size} bytes"
                sent.result(timeout=45)
            finally:
                # Unblock the sending thread on failure before joining it.
                try:
                    client.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass  # The connection may already be fully closed/reset.


def udp_exchange(client: socket.socket, payload: bytes) -> None:
    client.send(payload)
    assert client.recv(1473) == payload, "UDP echo mismatch or missing shutdown reply"


def udp_checks(port: int) -> int:
    count = 0
    # Two connected sockets share the relay port but must receive only their own replies.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as a, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as b:
        for client in (a, b):
            client.settimeout(5)
            client.connect(("127.0.0.1", port))
        for cycle in range(8):
            for size in (0, 1, 7, 64, 512, 1472):
                first = bytes((i + cycle) % 251 for i in range(size))
                second = bytes((i + cycle + 1) % 251 for i in range(size))
                a.send(first)
                b.send(second)
                assert a.recv(1473) == first, "UDP first-client mismatch"
                assert b.recv(1473) == second, "UDP second-client mismatch"
                count += 2
    return count


def stop_service(port: int) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
        client.settimeout(5)
        client.connect(("127.0.0.1", port))
        udp_exchange(client, b"quit")


def verify(port: int, check: Callable[[str, str], None], wait_for: WaitFor) -> None:
    check("start /bin/netecho\r", "netecho: ready")
    datagrams = udp_checks(port)
    count = 0
    for size in [0, 1, 7, 513, 4096, 65536, 262144] + [2048] * 16:
        transfer(port, size)
        count += 1
        if count % 4 == 0:
            check("echo interopalive\r", "\ninteropalive\n")
    with ThreadPoolExecutor(max_workers=1) as worker:
        stalled = worker.submit(transfer, port, 2 * 1024 * 1024, 8)
        check("echo interopreaderwaiting\r", "\ninteropreaderwaiting\n")
        check("uptime\r", "uptime: ")
        stalled.result(timeout=120)
    count += 1
    check("echo interoptransferdone\r", "\ninteroptransferdone\n")
    # A client reset must not make subsequent clients lose the service.
    with socket.socket() as aborted:
        aborted.settimeout(10)
        aborted.connect(("127.0.0.1", port))
        aborted.sendall(b"aborted-client" * 128)
        assert aborted.recv(64), "guest did not accept the client before reset"
        aborted.sendall(b"unread-before-reset" * 4096)
        aborted.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    wait_for("netecho: client failed; continuing")
    check("echo interopafterreset\r", "\ninteropafterreset\n")
    transfer(port, 8192)
    count += 2
    datagrams += udp_checks(port)
    stop_service(port)
    wait_for(f"netecho: stopped TCP={count} UDP={datagrams + 1}")
    check("start /bin/netecho\r", "netecho: ready")
    transfer(port, 8192)
    restarted_datagrams = udp_checks(port)
    stop_service(port)
    wait_for(f"netecho: stopped TCP=1 UDP={restarted_datagrams + 1}")
    check("echo interoppass\r", "\ninteroppass\n")
    print(
        f"PASS: localhost UDP relay, {datagrams + restarted_datagrams + 2} datagrams, payloads 0–1472, two clients, shutdown replies"
    )
    print(
        f"PASS: independent TCP stacks, {count + 1} sessions, 2 MiB delayed-reader transfer, EOF, client reset, service restart"
    )


def packet_summary(path: Path) -> str:
    """Verify TCP checksums/MSS and match every UDP relay request to its reply."""
    packets = data_bytes = zero_windows = peer_resets = 0
    udp_requests = udp_replies = 0
    udp_pending: Counter[tuple[int, bytes]] = Counter()
    udp_sizes: set[int] = set()
    syns: set[tuple[int, int]] = set()
    fins: set[tuple[int, int]] = set()
    peer_mss: dict[int, int] = {}
    with path.open("rb") as capture:
        header = capture.read(24)
        if len(header) != 24 or header[:4] not in (b"\xd4\xc3\xb2\xa1", b"\xa1\xb2\xc3\xd4"):
            raise AssertionError("invalid pcap header")
        endian = "<" if header[:4] == b"\xd4\xc3\xb2\xa1" else ">"
        assert struct.unpack(endian + "I", header[20:24])[0] == 1, "expected Ethernet capture"
        while record := capture.read(16):
            assert len(record) == 16, "truncated capture record"
            _, _, size, original = struct.unpack(endian + "IIII", record)
            assert size == original and 14 <= size <= 65535, "invalid capture length"
            frame = capture.read(size)
            assert len(frame) == size, "truncated captured frame"
            if frame[12:14] != b"\x08\x00":
                continue
            ip = frame[14:]
            assert len(ip) >= 20
            length = (ip[0] & 15) * 4
            total = struct.unpack("!H", ip[2:4])[0]
            assert 20 <= length <= total <= len(ip) and checksum(ip[:length]) == 0
            if ip[9] == 17:
                udp = ip[length:total]
                assert len(udp) >= 8
                source, dest, udp_length, check = struct.unpack("!HHHH", udp[:8])
                assert udp_length == len(udp) and 8 <= udp_length <= 1480, "invalid captured UDP length"
                pseudo = ip[12:20] + struct.pack("!BBH", 0, 17, len(udp))
                assert not check or checksum(pseudo + udp) == 0, "invalid captured UDP checksum"
                if ip[12:20] == RELAY_IP + GUEST_IP and dest == 19091:
                    udp_pending[source, udp[8:]] += 1
                    udp_requests += 1
                    udp_sizes.add(len(udp) - 8)
                elif ip[12:20] == GUEST_IP + RELAY_IP and source == 19091:
                    key = (dest, udp[8:])
                    assert udp_pending[key] > 0, "unsolicited/mismatched UDP reply"
                    udp_pending[key] -= 1
                    udp_replies += 1
                else:
                    raise AssertionError("UDP escaped the fixed relay endpoints")
                continue
            if ip[9] != 6:
                continue
            tcp = ip[length:total]
            assert len(tcp) >= 20
            source, dest, seq, _, offset, flags, window, _, _ = struct.unpack("!HHIIBBHHH", tcp[:20])
            pseudo = ip[12:20] + struct.pack("!BBH", 0, 6, len(tcp))
            assert checksum(pseudo + tcp) == 0, "invalid captured TCP checksum"
            header_length = (offset >> 4) * 4
            assert 20 <= header_length <= len(tcp)
            packets += 1
            if dest == 19092 and flags & 2:
                syns.add((source, seq))
                mss = 536
                pos = 20
                while pos < header_length:
                    kind = tcp[pos]
                    if kind == 0:
                        break
                    if kind == 1:
                        pos += 1
                        continue
                    assert pos + 1 < header_length
                    option_length = tcp[pos + 1]
                    assert option_length >= 2 and pos + option_length <= header_length
                    if kind == 2:
                        assert option_length == 4
                        mss = struct.unpack("!H", tcp[pos + 2 : pos + 4])[0]
                    pos += option_length
                peer_mss[source] = mss
            if flags & 1:
                fins.add((source, dest))
            if source == 19092:
                payload = len(tcp) - header_length
                assert payload <= peer_mss.get(dest, 536), "guest exceeded peer MSS"
                data_bytes += payload
            if dest == 19092 and flags & 4:
                peer_resets += 1
            if dest == 19092 and flags & 16 and not flags & 4 and window == 0:
                zero_windows += 1
    assert udp_requests == udp_replies and udp_replies >= 290 and not +udp_pending, "missing UDP reply evidence"
    assert {0, 1, 7, 64, 512, 1472} <= udp_sizes, "missing UDP boundary sizes"
    assert peer_resets, "missing guest-side reset evidence"
    assert len(syns) >= 27 and len(fins) >= 50, "missing handshake/FIN evidence"
    assert data_bytes >= 2 * 1024 * 1024, "missing large-transfer packet evidence"
    return (
        f"TCP capture: {packets} packets, {len(syns)} handshakes, {data_bytes} guest payload bytes, "
        f"peer MSS={sorted(set(peer_mss.values()))}, peer zero-window packets={zero_windows}, resets={peer_resets}; "
        f"UDP capture: {udp_requests} requests, {udp_replies} matching replies"
    )
