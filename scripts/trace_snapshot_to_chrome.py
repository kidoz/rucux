#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import argparse
import json
import struct
import sys
from typing import Optional


SNAPSHOT_HEADER_STRUCT = struct.Struct("<IHHIIIIQQQ")
SNAPSHOT_RECORD_STRUCT = struct.Struct("<QQQQIIHHI")
EXPORT_HEADER_STRUCT = struct.Struct("<IHHIIQ")
EXPORT_SECTION_STRUCT = struct.Struct("<IIIIIII16s")
PRODUCER_HEADER_STRUCT = struct.Struct("<IHHIIIIIIQ")
PRODUCER_RECORD_STRUCT = struct.Struct("<QQQQIH2x")

TRACE_SNAPSHOT_MAGIC = 0x54525543
TRACED_EXPORT_MAGIC = 0x54584558

TRACE_CLOCK_TSC_RAW = 1
TRACE_CLOCK_CNTVCT_RAW = 2

TRACED_EXPORT_SECTION_KERNEL_SNAPSHOT = 1
TRACED_EXPORT_SECTION_PRODUCER_RECORDS = 2

TRACED_EXPORT_CLOCK_KERNEL_RAW = 1
TRACED_EXPORT_CLOCK_MONOTONIC_NS = 2

TRACE_PRODUCER_EVENT_NAMES = {
    0x1001: "CONSOLE_RX",
    0x1002: "CONSOLE_TX",
    0x2001: "NET_REQ",
    0x2002: "NET_RESP",
}

EVENT_NAMES = {
    1: "BOOT",
    2: "ENQUEUE",
    3: "SWITCH",
    4: "BLOCK",
    5: "EXIT",
    6: "IRQ_TIMER",
    7: "IRQ_WAKE",
    8: "SYSCALL_IN",
    9: "SYSCALL_OUT",
    10: "PAGE_FAULT",
    11: "TRACE_CTL",
    12: "IPC_SEND_SYNC",
    13: "IPC_SEND_ASYNC",
    14: "IPC_RECV_SYNC",
    15: "IPC_CALL",
    16: "IPC_REPLY",
    17: "IPC_WAIT",
    18: "VFS_OPEN",
    19: "VFS_READ",
    20: "VFS_WRITE",
    21: "VFS_CLOSE",
    22: "CRASH",
}

CLOCK_ZERO_FREQ_IDS = {TRACE_CLOCK_TSC_RAW}

CLOCK_NAMES = {
    0: "none",
    TRACE_CLOCK_TSC_RAW: "tsc_raw",
    TRACE_CLOCK_CNTVCT_RAW: "cntvct_raw",
}

EXPORT_CLOCK_NAMES = {
    TRACED_EXPORT_CLOCK_KERNEL_RAW: "kernel_raw",
    TRACED_EXPORT_CLOCK_MONOTONIC_NS: "monotonic_ns",
}

PRODUCER_CATEGORY_NAMES = {
    0: "generic",
    1: "service",
    2: "driver",
    3: "app",
}

SYSCALL_NAMES = {
    0: "exit",
    1: "write",
    2: "read",
    3: "open",
    4: "close",
    5: "getdents",
    6: "ipc_send",
    7: "ipc_recv",
    8: "outb",
    9: "inb",
    10: "irq_wait",
    11: "mmap",
    12: "munmap",
    13: "socket",
    14: "bind",
    15: "listen",
    16: "accept",
    17: "connect",
    18: "send",
    19: "recv",
    20: "clone",
    21: "futex",
    22: "yield",
    23: "ioctl",
    24: "clock_gettime",
    25: "poll",
    26: "select",
    27: "epoll_create",
    28: "epoll_ctl",
    29: "epoll_wait",
    30: "mprotect",
    31: "msync",
    32: "madvise",
    33: "lseek",
    34: "stat",
    35: "fstat",
    36: "ftruncate",
    37: "fsync",
    38: "fcntl",
    39: "setsockopt",
    40: "getsockopt",
    41: "getsockname",
    42: "getpeername",
    43: "sendto",
    44: "recvfrom",
    45: "sigaction",
    46: "kill",
    47: "sigprocmask",
    48: "nanosleep",
    49: "gettimeofday",
    50: "ipc_call",
    51: "ipc_reply",
    52: "top",
    53: "trace_ctl",
}


def parse_snapshot_bytes(data: bytes):
    if len(data) < SNAPSHOT_HEADER_STRUCT.size:
        raise ValueError("snapshot too small")

    header = SNAPSHOT_HEADER_STRUCT.unpack_from(data, 0)
    magic, version, header_size, clock_id, cpu_count, record_size, record_count, clock_freq_hz, records_written, records_overwritten = header
    if magic != TRACE_SNAPSHOT_MAGIC:
        raise ValueError("invalid snapshot magic")
    if record_size != SNAPSHOT_RECORD_STRUCT.size:
        raise ValueError(f"unexpected record size {record_size}")

    records = []
    offset = header_size
    for _ in range(record_count):
        if offset + SNAPSHOT_RECORD_STRUCT.size > len(data):
            break
        records.append(SNAPSHOT_RECORD_STRUCT.unpack_from(data, offset))
        offset += SNAPSHOT_RECORD_STRUCT.size

    return {
        "format": "snapshot",
        "version": version,
        "clock_id": clock_id,
        "cpu_count": cpu_count,
        "record_count": len(records),
        "clock_freq_hz": clock_freq_hz,
        "records_written": records_written,
        "records_overwritten": records_overwritten,
        "records": records,
    }


def load_snapshot(path: str):
    data = open(path, "rb").read()
    return parse_snapshot_bytes(data)


def parse_export(path: str):
    data = open(path, "rb").read()
    if len(data) < EXPORT_HEADER_STRUCT.size:
        raise ValueError("export too small")

    magic, version, header_size, section_count, _reserved0, total_size = EXPORT_HEADER_STRUCT.unpack_from(data, 0)
    if magic != TRACED_EXPORT_MAGIC:
        raise ValueError("invalid export magic")

    sections = []
    offset = header_size
    for index in range(section_count):
        if offset + EXPORT_SECTION_STRUCT.size > len(data):
            raise ValueError("truncated export section header")

        section_header = EXPORT_SECTION_STRUCT.unpack_from(data, offset)
        section_type, section_size, clock_id, producer_id, category, tid, record_count, raw_name = section_header
        if section_size < EXPORT_SECTION_STRUCT.size or offset + section_size > len(data):
            raise ValueError(f"invalid export section size at index {index}")

        payload_offset = offset + EXPORT_SECTION_STRUCT.size
        payload = data[payload_offset:offset + section_size]
        name = raw_name.split(b"\0", 1)[0].decode("utf-8", errors="replace")

        if section_type == TRACED_EXPORT_SECTION_KERNEL_SNAPSHOT:
            snapshot = parse_snapshot_bytes(payload)
            sections.append({
                "type": "kernel_snapshot",
                "clock_id": clock_id,
                "snapshot": snapshot,
            })
        elif section_type == TRACED_EXPORT_SECTION_PRODUCER_RECORDS:
            records = []
            record_offset = 0
            while record_offset + PRODUCER_RECORD_STRUCT.size <= len(payload):
                records.append(PRODUCER_RECORD_STRUCT.unpack_from(payload, record_offset))
                record_offset += PRODUCER_RECORD_STRUCT.size

            sections.append({
                "type": "producer_records",
                "clock_id": clock_id,
                "producer_id": producer_id,
                "category": category,
                "tid": tid,
                "record_count": min(record_count, len(records)),
                "name": name,
                "records": records[:record_count],
            })
        else:
            sections.append({
                "type": f"unknown_{section_type}",
                "clock_id": clock_id,
                "payload_size": len(payload),
            })

        offset += section_size

    return {
        "format": "export",
        "version": version,
        "section_count": section_count,
        "total_size": total_size,
        "sections": sections,
    }


def load_input(path: str):
    data = open(path, "rb").read(4)
    if len(data) < 4:
        raise ValueError("input too small")

    magic = struct.unpack("<I", data)[0]
    if magic == TRACE_SNAPSHOT_MAGIC:
        return load_snapshot(path)
    if magic == TRACED_EXPORT_MAGIC:
        return parse_export(path)
    raise ValueError("unknown input format")


def convert_raw_timestamp(timestamp: int, base: int, clock_freq_hz: int, override_freq_hz: Optional[int]) -> float:
    freq = override_freq_hz if override_freq_hz else clock_freq_hz
    delta = timestamp - base
    if freq:
        return (delta * 1_000_000.0) / float(freq)
    return float(delta)


def convert_monotonic_ns(timestamp_ns: int, base_ns: int) -> float:
    return float(timestamp_ns - base_ns) / 1000.0


def convert_snapshot_records(snapshot, override_freq_hz: Optional[int], *, domain_label: str):
    records = snapshot["records"]
    if not records:
        return []

    base_ts = min(rec[0] for rec in records)
    trace_events = []
    pending_syscalls = {}
    pending_ipc_calls = {}

    for rec in records:
        timestamp, seq_no, arg0, arg1, cpu_id, thread_id, rec_type, event, _reserved = rec
        name = EVENT_NAMES.get(event, f"EVENT_{event}")
        ts = convert_raw_timestamp(timestamp, base_ts, snapshot["clock_freq_hz"], override_freq_hz)

        if event == 8:  # SYSCALL_IN
            pending_syscalls[(cpu_id, thread_id)] = {
                "ts": ts,
                "sysno": int(arg0),
                "seq_no": int(seq_no),
            }
            continue

        if event == 9:  # SYSCALL_OUT
            key = (cpu_id, thread_id)
            pending = pending_syscalls.pop(key, None)
            if pending is not None:
                sysno = pending["sysno"]
                trace_events.append({
                    "name": f"sys_{SYSCALL_NAMES.get(sysno, str(sysno))}",
                    "cat": "syscall",
                    "ph": "X",
                    "pid": int(cpu_id),
                    "tid": int(thread_id),
                    "ts": pending["ts"],
                    "dur": max(0.0, ts - pending["ts"]),
                    "args": {
                        "sysno": sysno,
                        "return": int(arg1),
                        "enter_seq_no": pending["seq_no"],
                        "exit_seq_no": int(seq_no),
                        "clock": CLOCK_NAMES.get(snapshot["clock_id"], str(snapshot["clock_id"])),
                        "clock_domain": domain_label,
                    },
                })
                continue

        if event == 15:  # IPC_CALL
            pending_ipc_calls[int(thread_id)] = {
                "ts": ts,
                "seq_no": int(seq_no),
                "target_tid": int(arg0),
                "msg_type": int(arg1),
                "cpu_id": int(cpu_id),
            }
            flow_id = f"kernel-{seq_no}"
            trace_events.append({
                "name": "ipc_call",
                "cat": "ipc",
                "ph": "s",
                "pid": int(cpu_id),
                "tid": int(thread_id),
                "ts": ts,
                "id": flow_id,
                "args": {
                    "target_tid": int(arg0),
                    "msg_type": int(arg1),
                    "clock_domain": domain_label,
                },
            })
            continue

        if event == 16:  # IPC_REPLY
            caller_tid = int(arg0)
            pending = pending_ipc_calls.pop(caller_tid, None)
            if pending is not None:
                trace_events.append({
                    "name": "ipc_call",
                    "cat": "ipc",
                    "ph": "f",
                    "pid": int(cpu_id),
                    "tid": int(thread_id),
                    "ts": ts,
                    "id": f"kernel-{pending['seq_no']}",
                    "bp": "e",
                    "args": {
                        "caller_tid": caller_tid,
                        "msg_type": int(arg1),
                        "call_cpu": pending["cpu_id"],
                        "target_tid": pending["target_tid"],
                        "clock_domain": domain_label,
                    },
                })
            trace_events.append({
                "name": "ipc_reply",
                "cat": "ipc",
                "ph": "i",
                "s": "t",
                "pid": int(cpu_id),
                "tid": int(thread_id),
                "ts": ts,
                "args": {
                    "caller_tid": caller_tid,
                    "msg_type": int(arg1),
                    "clock_domain": domain_label,
                },
            })
            continue

        trace_events.append({
            "name": name,
            "cat": "rucux",
            "ph": "i",
            "s": "t",
            "pid": int(cpu_id),
            "tid": int(thread_id),
            "ts": ts,
            "args": {
                "seq_no": int(seq_no),
                "arg0": int(arg0),
                "arg1": int(arg1),
                "record_type": int(rec_type),
                "clock": CLOCK_NAMES.get(snapshot["clock_id"], str(snapshot["clock_id"])),
                "clock_domain": domain_label,
            },
        })

    for (cpu_id, thread_id), pending in pending_syscalls.items():
        sysno = pending["sysno"]
        trace_events.append({
            "name": f"sys_{SYSCALL_NAMES.get(sysno, str(sysno))}_unterminated",
            "cat": "syscall",
            "ph": "i",
            "s": "t",
            "pid": int(cpu_id),
            "tid": int(thread_id),
            "ts": pending["ts"],
            "args": {
                "sysno": sysno,
                "enter_seq_no": pending["seq_no"],
                "warning": "missing syscall exit event in snapshot window",
                "clock_domain": domain_label,
            },
        })

    return trace_events


def convert_producer_section(section):
    records = section["records"]
    if not records:
        return []

    base_ts = min(rec[0] for rec in records)
    pid = 10_000 + int(section["producer_id"])
    tid = int(section["tid"]) if section["tid"] else int(section["producer_id"])

    trace_events = []
    trace_events.append({
        "name": "process_name",
        "ph": "M",
        "pid": pid,
        "tid": tid,
        "args": {
            "name": f"producer:{section['name'] or section['producer_id']}",
            "category": PRODUCER_CATEGORY_NAMES.get(section["category"], str(section["category"])),
        },
    })
    trace_events.append({
        "name": "thread_name",
        "ph": "M",
        "pid": pid,
        "tid": tid,
        "args": {
            "name": f"{section['name'] or section['producer_id']}:main",
        },
    })

    pending_net_request = None

    for rec in records:
        timestamp_ns, seq_no, arg0, arg1, producer_id, event = rec
        ts = convert_monotonic_ns(timestamp_ns, base_ts)

        if event == 0x2001:  # NET_REQ
            pending_net_request = {
                "ts": ts,
                "seq_no": int(seq_no),
                "request_type": int(arg0),
                "sender_tid": int(arg1),
            }
            continue

        if event == 0x2002 and pending_net_request is not None:  # NET_RESP
            request_type = pending_net_request["request_type"]
            trace_events.append({
                "name": f"net_{SYSCALL_NAMES.get(request_type, str(request_type))}",
                "cat": f"producer.{PRODUCER_CATEGORY_NAMES.get(section['category'], 'unknown')}",
                "ph": "X",
                "pid": pid,
                "tid": tid,
                "ts": pending_net_request["ts"],
                "dur": max(0.0, ts - pending_net_request["ts"]),
                "args": {
                    "request_seq_no": pending_net_request["seq_no"],
                    "response_seq_no": int(seq_no),
                    "request_type": request_type,
                    "sender_tid": pending_net_request["sender_tid"],
                    "result": int(arg1),
                    "producer_id": int(producer_id),
                    "producer_name": section["name"],
                    "clock_domain": EXPORT_CLOCK_NAMES.get(section["clock_id"], str(section["clock_id"])),
                },
            })
            trace_events.append({
                "name": "net_request_response",
                "cat": "producer.flow",
                "ph": "s",
                "pid": pid,
                "tid": tid,
                "ts": pending_net_request["ts"],
                "id": f"producer-{section['producer_id']}-net-{pending_net_request['seq_no']}",
                "args": {
                    "request_type": request_type,
                    "sender_tid": pending_net_request["sender_tid"],
                },
            })
            trace_events.append({
                "name": "net_request_response",
                "cat": "producer.flow",
                "ph": "f",
                "pid": pid,
                "tid": tid,
                "ts": ts,
                "id": f"producer-{section['producer_id']}-net-{pending_net_request['seq_no']}",
                "bp": "e",
                "args": {
                    "request_type": request_type,
                    "result": int(arg1),
                },
            })
            pending_net_request = None
            continue

        trace_events.append({
            "name": TRACE_PRODUCER_EVENT_NAMES.get(event, f"PRODUCER_{event}"),
            "cat": f"producer.{PRODUCER_CATEGORY_NAMES.get(section['category'], 'unknown')}",
            "ph": "i",
            "s": "t",
            "pid": pid,
            "tid": tid,
            "ts": ts,
            "args": {
                "seq_no": int(seq_no),
                "arg0": int(arg0),
                "arg1": int(arg1),
                "producer_id": int(producer_id),
                "producer_name": section["name"],
                "clock_domain": EXPORT_CLOCK_NAMES.get(section["clock_id"], str(section["clock_id"])),
            },
        })

    if pending_net_request is not None:
        trace_events.append({
            "name": f"net_{SYSCALL_NAMES.get(pending_net_request['request_type'], str(pending_net_request['request_type']))}_unterminated",
            "cat": f"producer.{PRODUCER_CATEGORY_NAMES.get(section['category'], 'unknown')}",
            "ph": "i",
            "s": "t",
            "pid": pid,
            "tid": tid,
            "ts": pending_net_request["ts"],
            "args": {
                "request_seq_no": pending_net_request["seq_no"],
                "request_type": pending_net_request["request_type"],
                "sender_tid": pending_net_request["sender_tid"],
                "warning": "missing NET_RESP event in export window",
                "clock_domain": EXPORT_CLOCK_NAMES.get(section["clock_id"], str(section["clock_id"])),
            },
        })

    return trace_events


def convert_snapshot(snapshot, override_freq_hz: Optional[int]):
    trace_events = convert_snapshot_records(snapshot, override_freq_hz, domain_label="kernel_raw")
    return {
        "traceEvents": trace_events,
        "displayTimeUnit": "ns",
        "metadata": {
            "format": "snapshot",
            "clock": CLOCK_NAMES.get(snapshot["clock_id"], str(snapshot["clock_id"])),
            "clock_freq_hz": snapshot["clock_freq_hz"],
            "clock_requires_override": snapshot["clock_id"] in CLOCK_ZERO_FREQ_IDS and not snapshot["clock_freq_hz"],
            "records_written": snapshot["records_written"],
            "records_overwritten": snapshot["records_overwritten"],
            "cpu_count": snapshot["cpu_count"],
        },
    }


def convert_export(export, override_freq_hz: Optional[int]):
    trace_events = []
    metadata_sections = []

    trace_events.append({
        "name": "clock_domains_not_globally_aligned",
        "cat": "rucux",
        "ph": "i",
        "s": "g",
        "pid": 0,
        "tid": 0,
        "ts": 0,
        "args": {
            "kernel": "kernel_raw",
            "userspace": "monotonic_ns",
            "note": "section timestamps are converted per clock domain and are not globally correlated",
        },
    })

    for section in export["sections"]:
        if section["type"] == "kernel_snapshot":
            snapshot = section["snapshot"]
            trace_events.extend(
                convert_snapshot_records(
                    snapshot,
                    override_freq_hz,
                    domain_label=EXPORT_CLOCK_NAMES.get(section["clock_id"], str(section["clock_id"])),
                )
            )
            metadata_sections.append({
                "type": "kernel_snapshot",
                "clock": CLOCK_NAMES.get(snapshot["clock_id"], str(snapshot["clock_id"])),
                "export_clock": EXPORT_CLOCK_NAMES.get(section["clock_id"], str(section["clock_id"])),
                "records_written": snapshot["records_written"],
                "records_overwritten": snapshot["records_overwritten"],
                "cpu_count": snapshot["cpu_count"],
            })
        elif section["type"] == "producer_records":
            trace_events.extend(convert_producer_section(section))
            metadata_sections.append({
                "type": "producer_records",
                "producer_id": section["producer_id"],
                "name": section["name"],
                "category": PRODUCER_CATEGORY_NAMES.get(section["category"], str(section["category"])),
                "tid": section["tid"],
                "record_count": section["record_count"],
                "clock": EXPORT_CLOCK_NAMES.get(section["clock_id"], str(section["clock_id"])),
            })
        else:
            metadata_sections.append(section)

    return {
        "traceEvents": trace_events,
        "displayTimeUnit": "ns",
        "metadata": {
            "format": "export",
            "section_count": export["section_count"],
            "total_size": export["total_size"],
            "sections": metadata_sections,
        },
    }


def convert_input(trace_input, override_freq_hz: Optional[int]):
    if trace_input["format"] == "snapshot":
        return convert_snapshot(trace_input, override_freq_hz)
    if trace_input["format"] == "export":
        return convert_export(trace_input, override_freq_hz)
    raise ValueError(f"unsupported input format {trace_input['format']}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert rucux trace snapshot or traced export binaries to Chrome/Perfetto JSON")
    parser.add_argument("input", help="input snapshot/export binary")
    parser.add_argument("-o", "--output", help="output JSON file; defaults to stdout")
    parser.add_argument("--clock-freq-hz", type=int, default=None, help="override clock frequency for raw clocks like TSC")
    args = parser.parse_args()

    trace_input = load_input(args.input)
    converted = convert_input(trace_input, args.clock_freq_hz)
    out = json.dumps(converted, indent=2)

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(out)
            f.write("\n")
    else:
        sys.stdout.write(out)
        sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
