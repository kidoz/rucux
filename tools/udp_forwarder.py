# SPDX-License-Identifier: MIT
"""Fixed localhost UDP echo-test relay; no routing or guest-initiated sockets.

libslirp's restricted mode discards ordinary guest UDP before its forwarding
lookup. Keep that restriction for TCP and use an inherited Ethernet socket for
this single UDP endpoint instead. Both host and guest destinations are fixed.
"""

import select
import socket
import struct
import threading
import time

from network_peer import GUEST_IP, GUEST_MAC, checksum

RELAY_IP = bytes([10, 0, 0, 4])
RELAY_MAC = bytes.fromhex("525400654322")
GUEST_PORT = 19091
MAX_CLIENTS = 64
CLIENT_LIFETIME = 30


class UdpForwarder:
    def __init__(self, wire: socket.socket, port: int) -> None:
        self.wire = wire
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.udp.bind(("127.0.0.1", port))
        except BaseException:
            self.udp.close()
            raise
        self.udp.settimeout(1)
        self.wire.settimeout(1)
        self.clients: dict[int, float] = {}
        self.buffer = b""
        self.stopping = threading.Event()
        self.error: Exception | None = None
        self.thread = threading.Thread(target=self.run, name="udp-echo-relay")

    def start(self) -> None:
        self.thread.start()

    def close(self) -> None:
        self.stopping.set()
        if self.thread.ident is not None:
            self.thread.join(timeout=2)
            if self.thread.is_alive():
                raise RuntimeError("UDP relay did not stop")
        self.udp.close()

    def check(self) -> None:
        if self.error is not None:
            raise RuntimeError("UDP relay failed") from self.error

    def send_frame(self, ethertype: int, payload: bytes) -> None:
        frame = (GUEST_MAC + RELAY_MAC + struct.pack("!H", ethertype) + payload).ljust(60, b"\0")
        self.wire.sendall(struct.pack("!I", len(frame)) + frame)

    def host_input(self) -> None:
        data, (address, port) = self.udp.recvfrom(1473)
        if address != "127.0.0.1" or len(data) > 1472:
            return
        now = time.monotonic()
        self.clients = {key: expiry for key, expiry in self.clients.items() if expiry > now}
        if port not in self.clients and len(self.clients) >= MAX_CLIENTS:
            return
        self.clients[port] = now + CLIENT_LIFETIME
        udp = struct.pack("!HHHH", port, GUEST_PORT, len(data) + 8, 0) + data
        pseudo = RELAY_IP + GUEST_IP + struct.pack("!BBH", 0, 17, len(udp))
        udp = udp[:6] + struct.pack("!H", checksum(pseudo + udp) or 65535) + udp[8:]
        ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, len(udp) + 20, 0, 0, 64, 17, 0, RELAY_IP, GUEST_IP)
        ip = ip[:10] + struct.pack("!H", checksum(ip)) + ip[12:]
        self.send_frame(0x0800, ip + udp)

    def guest_input(self, frame: bytes) -> None:
        if len(frame) < 14 or frame[6:12] != GUEST_MAC:
            return
        if frame[12:14] == b"\x08\x06":
            if len(frame) < 42:
                return
            arp = frame[14:42]
            if arp[:8] != struct.pack("!HHBBH", 1, 0x0800, 6, 4, 1):
                return
            if arp[8:14] != GUEST_MAC or arp[14:18] != GUEST_IP or arp[24:28] != RELAY_IP:
                return
            reply = struct.pack("!HHBBH6s4s6s4s", 1, 0x0800, 6, 4, 2, RELAY_MAC, RELAY_IP, GUEST_MAC, GUEST_IP)
            self.send_frame(0x0806, reply)
            return
        if frame[:6] != RELAY_MAC or frame[12:14] != b"\x08\x00":
            return
        ip = frame[14:]
        if len(ip) < 20 or ip[0] >> 4 != 4 or ip[9] != 17 or ip[12:20] != GUEST_IP + RELAY_IP:
            return
        header = (ip[0] & 15) * 4
        total, _, fragment = struct.unpack("!HHH", ip[2:8])
        if not 20 <= header <= total <= len(ip) or fragment & 0xBFFF or checksum(ip[:header]):
            return
        udp = ip[header:total]
        if len(udp) < 8:
            return
        source, dest, length, check = struct.unpack("!HHHH", udp[:8])
        if source != GUEST_PORT or not 8 <= length <= len(udp) or length > 1480:
            return
        if self.clients.get(dest, 0) <= time.monotonic():
            return
        pseudo = GUEST_IP + RELAY_IP + struct.pack("!BBH", 0, 17, length)
        if check and checksum(pseudo + udp[:length]):
            return
        self.udp.sendto(udp[8:length], ("127.0.0.1", dest))

    def run(self) -> None:
        try:
            while not self.stopping.is_set():
                ready, _, _ = select.select([self.udp, self.wire], [], [], 0.05)
                if self.udp in ready:
                    self.host_input()
                if self.wire in ready:
                    data = self.wire.recv(65536)
                    if not data:
                        return
                    self.buffer += data
                    while len(self.buffer) >= 4:
                        size = struct.unpack("!I", self.buffer[:4])[0]
                        if not 14 <= size <= 1514:
                            raise RuntimeError("invalid Ethernet frame length")
                        if len(self.buffer) < 4 + size:
                            break
                        frame, self.buffer = self.buffer[4 : 4 + size], self.buffer[4 + size :]
                        self.guest_input(frame)
        except Exception as error:
            self.error = error
