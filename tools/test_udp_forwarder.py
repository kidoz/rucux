# SPDX-License-Identifier: MIT
"""Localhost-only regression checks for the UDP test relay's forwarding bounds."""

import socket
import struct
import time
import unittest

from network_peer import GUEST_IP, GUEST_MAC, checksum
from udp_forwarder import MAX_CLIENTS, RELAY_IP, RELAY_MAC, UdpForwarder


def reply(payload: bytes, destination: int, source: int = 19091) -> bytes:
    udp = struct.pack("!HHHH", source, destination, len(payload) + 8, 0) + payload
    pseudo = GUEST_IP + RELAY_IP + struct.pack("!BBH", 0, 17, len(udp))
    udp = udp[:6] + struct.pack("!H", checksum(pseudo + udp) or 65535) + udp[8:]
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, len(udp) + 20, 0, 0, 64, 17, 0, GUEST_IP, RELAY_IP)
    ip = ip[:10] + struct.pack("!H", checksum(ip)) + ip[12:]
    return RELAY_MAC + GUEST_MAC + b"\x08\x00" + ip + udp


class RelayTests(unittest.TestCase):
    def setUp(self) -> None:
        self.wire, self.peer = socket.socketpair()
        self.relay = UdpForwarder(self.wire, 0)
        self.client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.client.bind(("127.0.0.1", 0))
        self.client.connect(self.relay.udp.getsockname())
        self.client.settimeout(0.02)
        self.peer.settimeout(0.02)
        self.port = self.client.getsockname()[1]

    def tearDown(self) -> None:
        self.relay.close()
        self.client.close()
        self.peer.close()
        self.wire.close()

    def frame(self) -> bytes:
        header = self.peer.recv(4)
        self.assertEqual(len(header), 4)
        size = struct.unpack("!I", header)[0]
        data = b""
        while len(data) < size:
            data += self.peer.recv(size - len(data))
        return data

    def test_empty_and_maximum_datagrams_and_optional_reply_checksum(self) -> None:
        for payload in (b"", bytes(i % 251 for i in range(1472))):
            self.client.send(payload)
            self.relay.host_input()
            frame = self.frame()
            self.assertEqual(frame[:14], GUEST_MAC + RELAY_MAC + b"\x08\x00")
            total = struct.unpack("!H", frame[16:18])[0]
            self.assertEqual(frame[26:34], RELAY_IP + GUEST_IP)
            self.assertEqual(checksum(frame[14:34]), 0)
            udp = frame[34 : 14 + total]
            self.assertEqual(udp[8:], payload)
            self.assertEqual(checksum(RELAY_IP + GUEST_IP + struct.pack("!BBH", 0, 17, len(udp)) + udp), 0)
            for checked in (True, False):
                packet = bytearray(reply(payload, self.port))
                if not checked:
                    packet[40:42] = b"\0\0"
                self.relay.guest_input(bytes(packet))
                self.assertEqual(self.client.recv(1473), payload)

    def test_rejects_unrequested_endpoints_and_malformed_replies(self) -> None:
        valid = reply(b"data", self.port)
        self.relay.guest_input(valid)  # No host request registered this port.
        self.relay.clients[self.port] = time.monotonic() + 30
        bad = [reply(b"data", self.port, source=19092), valid[:25]]
        for offset in (0, 6, 26, 30, 40):  # MACs, IPs and UDP checksum.
            packet = bytearray(valid)
            packet[offset] ^= 1
            bad.append(bytes(packet))
        # A checksum-valid fragmented IPv4 packet must still be rejected.
        packet = bytearray(valid)
        packet[20:22] = b"\x20\0"
        packet[24:26] = b"\0\0"
        packet[24:26] = struct.pack("!H", checksum(packet[14:34]))
        bad.append(bytes(packet))
        for packet_bytes in bad:
            self.relay.guest_input(packet_bytes)
        with self.assertRaises(TimeoutError):
            self.client.recv(1473)

    def test_client_limit_and_expiry_bound_forwarding(self) -> None:
        self.relay.clients = {port: time.monotonic() + 30 for port in range(1, MAX_CLIENTS + 1)}
        self.client.send(b"full")
        self.relay.host_input()
        self.assertEqual(len(self.relay.clients), MAX_CLIENTS)
        with self.assertRaises(TimeoutError):
            self.peer.recv(4)
        self.relay.clients = dict.fromkeys(self.relay.clients, 0.0)
        self.client.send(b"reopened")
        self.relay.host_input()
        self.frame()
        self.assertEqual(set(self.relay.clients), {self.port})
        self.relay.clients[self.port] = 0
        self.relay.guest_input(reply(b"expired", self.port))
        with self.assertRaises(TimeoutError):
            self.client.recv(1473)

    def test_oversize_request_is_not_truncated_and_forwarded(self) -> None:
        self.client.send(bytes(1473))
        self.relay.host_input()
        self.assertFalse(self.relay.clients)
        with self.assertRaises(TimeoutError):
            self.peer.recv(4)

    def test_arp_answers_only_the_fixed_guest_and_relay_address(self) -> None:
        arp = struct.pack("!HHBBH6s4s6s4s", 1, 0x0800, 6, 4, 1, GUEST_MAC, GUEST_IP, bytes(6), RELAY_IP)
        request = bytes([255]) * 6 + GUEST_MAC + b"\x08\x06" + arp
        self.relay.guest_input(request)
        frame = self.frame()
        self.assertEqual(frame[20:22], b"\0\2")
        self.assertEqual(frame[22:32], RELAY_MAC + RELAY_IP)
        self.relay.guest_input(request[:-1] + b"\x05")
        with self.assertRaises(TimeoutError):
            self.peer.recv(4)

    def test_invalid_frame_length_stops_and_reports_worker_failure(self) -> None:
        self.relay.start()
        self.peer.sendall(struct.pack("!I", 65536))
        self.relay.thread.join(timeout=2)
        self.assertFalse(self.relay.thread.is_alive())
        with self.assertRaises(RuntimeError):
            self.relay.check()


if __name__ == "__main__":
    unittest.main()
