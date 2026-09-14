# SPDX-License-Identifier: MIT
"""Deterministic Ethernet peer for QEMU's length-prefixed, inherited socket FD.

No IP sockets, TAP devices, host interfaces, or external network are used.
"""

import select
import socket
import struct
import time
from collections.abc import Callable
from typing import BinaryIO, Protocol

GUEST_MAC = bytes.fromhex("525400123456")
PEER_MAC = bytes.fromhex("525400654321")
GUEST_IP = bytes([10, 0, 0, 10])
PEER_IP = bytes([10, 0, 0, 2])


def checksum(data: bytes | bytearray) -> int:
    data += b"\0" * (len(data) % 2)
    value: int = sum(struct.unpack(f"!{len(data) // 2}H", data))
    while value >> 16:
        value = (value & 65535) + (value >> 16)
    return (~value) & 65535


def ip_packet(
    protocol: int, payload: bytes | bytearray, *, ihl: int = 5, total: int | None = None, fragment: int = 0
) -> bytes:
    header = struct.pack(
        "!BBHHHBBH4s4s",
        0x40 | ihl,
        0,
        len(payload) + 20 if total is None else total,
        1,
        fragment,
        64,
        protocol,
        0,
        PEER_IP,
        GUEST_IP,
    )
    header = header[:10] + struct.pack("!H", checksum(header)) + header[12:]
    return header + payload


class Peer:
    def __init__(self, sock: socket.socket, capture: BinaryIO) -> None:
        self.sock = sock
        self.buffer = b""
        self.capture = capture
        capture.write(struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))

    def record(self, frame: bytes) -> None:
        now = time.time()
        self.capture.write(struct.pack("<IIII", int(now), int(now % 1 * 1e6), len(frame), len(frame)) + frame)
        self.capture.flush()

    def send(self, ethertype: int, payload: bytes | bytearray) -> None:
        frame = GUEST_MAC + PEER_MAC + struct.pack("!H", ethertype) + payload
        frame = frame.ljust(60, b"\0")
        self.record(frame)
        self.sock.sendall(struct.pack("!I", len(frame)) + frame)

    def receive(self, timeout: float = 3) -> bytes:
        deadline = time.monotonic() + timeout
        while True:
            if len(self.buffer) >= 4:
                size = struct.unpack("!I", self.buffer[:4])[0]
                if size > 65535:
                    raise RuntimeError("invalid QEMU frame length")
                if len(self.buffer) >= size + 4:
                    frame, self.buffer = self.buffer[4 : 4 + size], self.buffer[4 + size :]
                    self.record(frame)
                    return frame
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.sock], [], [], remaining)[0]:
                raise TimeoutError("Ethernet peer receive timeout")
            data = self.sock.recv(65536)
            if not data:
                raise RuntimeError("QEMU packet socket closed")
            self.buffer += data

    def arp(self) -> None:
        request = struct.pack("!HHBBH6s4s6s4s", 1, 0x0800, 6, 4, 1, PEER_MAC, PEER_IP, bytes(6), GUEST_IP)
        self.send(0x0806, request)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            frame = self.receive()
            if frame[12:14] == b"\x08\x06" and frame[20:22] == b"\0\2":
                assert frame[22:28] == GUEST_MAC and frame[28:32] == GUEST_IP, "incorrect ARP reply"
                return
        raise RuntimeError("missing ARP reply")

    def transport(self, protocol: int) -> bytes:
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            frame = self.receive(max(0.01, deadline - time.monotonic()))
            if frame[12:14] != b"\x08\x00":
                continue
            ip = frame[14:]
            header = (ip[0] & 15) * 4
            total = struct.unpack("!H", ip[2:4])[0]
            assert 20 <= header <= total <= len(ip) and checksum(ip[:header]) == 0, "invalid IPv4 output"
            assert ip[12:16] == GUEST_IP and ip[16:20] == PEER_IP, "incorrect route/address"
            if ip[9] == protocol:
                return ip[header:total]
        raise RuntimeError("missing transport reply")

    def cold_arp_udp(self) -> None:
        payload = b"first-packet-must-survive"
        self.send(0x0800, ip_packet(17, struct.pack("!HHHH", 40000, 19091, 8 + len(payload), 0) + payload))
        for attempt in range(2):
            frame = self.receive()
            assert frame[12:14] == b"\x08\x06" and frame[20:22] == b"\0\1", "expected ARP request"
            assert frame[28:32] == GUEST_IP and frame[38:42] == PEER_IP
            # Drop the first request; answer its timed retry.
        reply = struct.pack("!HHBBH6s4s6s4s", 1, 0x0800, 6, 4, 2, PEER_MAC, PEER_IP, GUEST_MAC, GUEST_IP)
        self.send(0x0806, reply)
        segment = self.transport(17)
        assert segment[8:] == payload, "ARP resolution lost the first UDP packet"

    def udp(self, payload: bytes) -> None:
        udp = struct.pack("!HHHH", 40000, 19091, len(payload) + 8, 0) + payload
        self.send(0x0800, ip_packet(17, udp))
        reply = self.transport(17)
        assert struct.unpack("!HHH", reply[:6]) == (19091, 40000, len(payload) + 8)
        assert reply[8:] == payload, "incorrect UDP echo or malformed packet accepted"

    def malformed(self) -> None:
        udp = struct.pack("!HHHH", 40000, 19091, 11, 0) + b"bad"
        for options in [{"ihl": 0}, {"ihl": 15}, {"total": 19}, {"total": 2000}, {"fragment": 0x2000}, {"fragment": 1}]:
            self.send(0x0800, ip_packet(17, udp, **options))
        for length, check in [(0, 0), (7, 0), (500, 0), (11, 1)]:
            self.send(0x0800, ip_packet(17, struct.pack("!HHHH", 40000, 19091, length, check) + b"bad"))
        bad_ip = bytearray(ip_packet(17, udp))
        bad_ip[10] ^= 1
        self.send(0x0800, bad_ip)
        # Invalid TCP data offsets with otherwise valid checksums must be dropped.
        for offset in [0, 4, 15]:
            segment = struct.pack("!HHIIBBHHH", 40001, 19092, 1, 0, offset << 4, 2, 65535, 0, 0)
            pseudo = PEER_IP + GUEST_IP + struct.pack("!BBH", 0, 6, len(segment))
            segment = segment[:16] + struct.pack("!H", checksum(pseudo + segment)) + segment[18:]
            self.send(0x0800, ip_packet(6, segment))
        try:
            frame = self.receive(0.15)
        except TimeoutError:
            return
        raise AssertionError(f"malformed packet produced response: {frame.hex()}")

    def tcp_echo(self, payload: bytes, port: int = 40001, loss: bool = False) -> None:
        sequence = 10000
        received_sequence = 0

        def send(flags: int, data: bytes = b"") -> None:
            nonlocal sequence
            segment = (
                struct.pack("!HHIIBBHHH", port, 19092, sequence, received_sequence, 5 << 4, flags, 65535, 0, 0) + data
            )
            pseudo = PEER_IP + GUEST_IP + struct.pack("!BBH", 0, 6, len(segment))
            segment = segment[:16] + struct.pack("!H", checksum(pseudo + segment)) + segment[18:]
            self.send(0x0800, ip_packet(6, segment))
            sequence += len(data) + bool(flags & 2) + bool(flags & 1)

        def receive() -> tuple[int, int, int, bytes]:
            segment = self.transport(6)
            pseudo = GUEST_IP + PEER_IP + struct.pack("!BBH", 0, 6, len(segment))
            assert checksum(pseudo + segment) == 0, "invalid TCP checksum"
            source, dest, seq, ack, offset, flags, _, _, _ = struct.unpack("!HHIIBBHHH", segment[:20])
            assert (source, dest) == (19092, port) and not flags & 4, "unexpected TCP reset/endpoint"
            assert ack <= sequence
            return seq, ack, flags, segment[(offset >> 4) * 4 :]

        send(2)
        seq, ack, flags, _ = receive()
        assert flags == 0x12 and ack == sequence, "missing SYN/ACK"
        if loss:
            # Drop the first SYN/ACK and verify its timed retransmission.
            repeated = receive()
            assert repeated[:3] == (seq, ack, flags), "SYN/ACK retransmission changed sequence"
        received_sequence = seq + 1
        if loss:
            # Also lose the handshake ACK; another SYN/ACK must recover it.
            repeated = receive()
            assert repeated[:3] == (seq, ack, flags)
        send(16)
        echo = b""
        dropped_data = dropped_ack = False
        for start in range(0, len(payload), 512):
            chunk = payload[start : start + 512]
            send(24, chunk)
            deadline = time.monotonic() + 3
            while len(echo) < start + len(chunk):
                assert time.monotonic() < deadline, "TCP data timeout"
                seq, _, flags, data = receive()
                assert seq == received_sequence and not flags & 1
                if data:
                    if loss and not dropped_data:
                        dropped_data = True
                        continue  # Lose data; the guest must resend the same sequence.
                    echo += data
                    received_sequence += len(data)
                    if loss and not dropped_ack:
                        dropped_ack = True
                        # Receive duplicate data after discarding the ACK. Never echo it twice.
                        duplicate = receive()
                        while not duplicate[3]:
                            duplicate = receive()
                        assert duplicate[0] == seq and duplicate[3] == data, "ACK loss changed retransmitted data"
                    send(16)
        assert echo == payload, "TCP stream mismatch"
        send(17)  # Half-close: server reads EOF and responds with FIN.
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            seq, ack, flags, data = receive()
            assert seq == received_sequence and not data
            if flags & 1:
                assert ack == sequence
                if loss:
                    repeated = receive()
                    assert repeated[:3] == (seq, ack, flags), "FIN retransmission changed sequence"
                received_sequence += 1
                if loss:
                    # Lose the final ACK too; resend it when FIN is repeated.
                    repeated = receive()
                    assert repeated[:3] == (seq, ack, flags)
                send(16)
                return
        raise RuntimeError("TCP teardown timeout")


class WaitFor(Protocol):
    def __call__(self, marker: str, start: int = 0, seconds: float = 12) -> str: ...


def verify(peer: Peer, check: Callable[[str, str], None], wait_for: WaitFor) -> None:
    check("start /bin/netecho\r", "netecho: ready")
    peer.cold_arp_udp()
    peer.arp()
    peer.malformed()
    for size in [0, 1, 7, 64, 512, 1472]:
        peer.udp(bytes(i % 251 for i in range(size)))
    for cycle in range(32):
        peer.tcp_echo(bytes((i + cycle) % 251 for i in range(2048)), port=40001)
        if cycle % 8 == 0:
            check("echo networkingalive\r", "\nnetworkingalive\n")
            peer.udp(f"cycle-{cycle}".encode())
    peer.tcp_echo(b"loss-recovery-" * 80, loss=True)
    check("echo recoveryalive\r", "\nrecoveryalive\n")
    peer.udp(b"quit")
    wait_for("netecho: stopped TCP=33 UDP=12")
    check("echo afterpeer\r", "\nafterpeer\n")
    # Rebind both server ports after teardown and exercise a second process.
    check("start /bin/netecho\r", "netecho: ready")
    peer.tcp_echo(b"reopened service")
    peer.udp(b"quit")
    wait_for("netecho: stopped TCP=1 UDP=1")
    check("echo peerpass\r", "\npeerpass\n")
