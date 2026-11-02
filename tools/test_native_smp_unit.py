#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise aggregate failure propagation, evidence auditing and child cleanup."""

from __future__ import annotations

import contextlib
import hashlib
import io
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

import test_amd64_smp_smoke as amd64
import test_armv7_smp_smoke as armv7
import test_retirement_smoke as aarch64
from test_native_smp import ROOT, Gate, GateError, Settings, audit, plans, run_child, run_suite, write_json

REVISION = "fixture-revision"


def fixture(gate: Gate, settings: Settings) -> Path:
    campaign = gate.directory / "campaigns" / "one"
    campaign.mkdir(parents=True)
    metadata: dict[str, object] = {"git_revision": REVISION, "network": "-nic none"}
    for name, field in (
        ("kernel.elf", "kernel_sha256"),
        ("kernel.bin", "binary_sha256"),
        ("BOOTX64.EFI", "loader_sha256"),
    ):
        data = (gate.arch + name).encode()
        (campaign / name).write_bytes(data)
        metadata[field] = hashlib.sha256(data).hexdigest()
    (campaign / "uefi.img").write_bytes(b"fixture disk")
    if gate.arch == "aarch64":
        metadata.update(seed=settings.seed, rounds=settings.rounds, single_cpu_compatibility=True)
    write_json(campaign / "metadata.json", metadata)
    records = []
    for index, cpus in enumerate([1] + [4] * settings.attempts):
        name = f"attempt-{index + 1:04d}" if gate.arch == "aarch64" else f"boot-{index:04d}-{cpus}cpu"
        boot = campaign / name
        boot.mkdir()
        binary = {"amd64": "qemu-system-x86_64", "armv7": "qemu-system-arm", "aarch64": "qemu-system-aarch64"}[
            gate.arch
        ]
        command = [
            binary,
            "-smp",
            str(cpus),
            "-nic",
            "none",
            "-monitor",
            "stdio",
            "-serial",
            f"file:{boot / 'serial.log'}",
        ]
        if gate.arch == "amd64":
            command += ["-drive", f"file={campaign / 'uefi.img'},if=ide,format=raw,snapshot=on"]
            markers = amd64.required_markers(cpus)
        elif gate.arch == "armv7":
            command += ["-kernel", str(campaign / "kernel.bin")]
            markers = armv7.required_markers(cpus)
        else:
            command += ["-kernel", str(campaign / "kernel.elf")]
            markers = aarch64.required_markers(settings.seed, settings.rounds, cpus)
        write_json(boot / "command.json", command)
        (boot / "serial.log").write_text("\n".join(markers))
        record: dict[str, object] = {"cpus": cpus, "exit_code": 0, "failure_marker": False}
        record.update({"result": "PASS"} if gate.arch == "aarch64" else {"pass": True})
        records.append(record)
    write_json(campaign / "results.json", records)
    return campaign


class NativeSmpTest(unittest.TestCase):
    def setUp(self) -> None:
        location = ROOT / "build/host-tests"
        location.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="native-smp-unit-", dir=location)
        self.work = Path(self.temp.name)
        self.settings = Settings(attempts=2, rounds=48)
        self.gates = plans(self.work, self.settings)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_each_architecture_has_isolated_builds_and_aarch64_compatibility(self) -> None:
        self.assertEqual([g.arch for g in self.gates], ["amd64", "armv7", "aarch64"])
        for gate in self.gates:
            self.assertIn(str(self.work / "builds" / gate.arch), gate.command)
            self.assertIn(str(gate.directory / "campaigns"), gate.command)
        self.assertIn("--with-single-cpu", self.gates[2].command)

    def test_complete_independent_evidence_passes_for_every_architecture(self) -> None:
        for gate in self.gates:
            campaign = fixture(gate, self.settings)
            self.assertEqual(audit(gate, self.settings, REVISION), (campaign, 3))

    def test_success_exit_without_evidence_fails_and_other_gates_still_run(self) -> None:
        calls = []

        def execute(command: tuple[str, ...], log: Path, timeout: float) -> int:
            calls.append(log.parent.name)
            if log.parent.name != "amd64":
                fixture(next(g for g in self.gates if g.directory == log.parent), self.settings)
            return 0

        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(run_suite(self.work, self.settings, REVISION, execute), 1)
        summary = json.loads((self.work / "summary.json").read_text())
        self.assertEqual(calls, ["amd64", "armv7", "aarch64"])
        self.assertEqual(summary["status"], "FAIL")
        self.assertEqual([r["status"] for r in summary["architectures"]], ["FAIL", "PASS", "PASS"])

    def test_nonzero_exit_dominates_even_complete_guest_evidence(self) -> None:
        def execute(command: tuple[str, ...], log: Path, timeout: float) -> int:
            fixture(next(g for g in self.gates if g.directory == log.parent), self.settings)
            return 7 if log.parent.name == "armv7" else 0

        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(run_suite(self.work, self.settings, REVISION, execute), 1)
        rows = json.loads((self.work / "summary.json").read_text())["architectures"]
        self.assertEqual(rows[1]["exit_code"], 7)
        self.assertEqual(rows[1]["status"], "FAIL")
        self.assertIn("status 7", rows[1]["error"])
        self.assertIn("armv7/gate.log", rows[1]["error"])
        self.assertEqual(rows[2]["status"], "PASS")

    def test_timeout_failure_is_retained_and_does_not_skip_later_gates(self) -> None:
        def execute(command: tuple[str, ...], log: Path, timeout: float) -> int:
            if log.parent.name == "amd64":
                log.write_text("build failed or stalled")
                raise GateError("stage deadline")
            fixture(next(g for g in self.gates if g.directory == log.parent), self.settings)
            return 0

        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(run_suite(self.work, self.settings, REVISION, execute), 1)
        summary = json.loads((self.work / "summary.json").read_text())
        self.assertEqual(summary["architectures"][0]["error"], "stage deadline")
        self.assertEqual(summary["architectures"][2]["status"], "PASS")
        self.assertTrue((self.work / "amd64/gate.log").is_file())

    def test_interruption_is_not_a_partial_pass_and_leaves_remaining_gates_not_run(self) -> None:
        def execute(command: tuple[str, ...], log: Path, timeout: float) -> int:
            raise KeyboardInterrupt

        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(run_suite(self.work, self.settings, REVISION, execute), 130)
        summary = json.loads((self.work / "summary.json").read_text())
        self.assertEqual(summary["status"], "INTERRUPTED")
        self.assertEqual([r["status"] for r in summary["architectures"]], ["INTERRUPTED", "NOT_RUN", "NOT_RUN"])

    def test_complete_suite_passes_and_marks_armv7_kernel_only(self) -> None:
        def execute(command: tuple[str, ...], log: Path, timeout: float) -> int:
            fixture(next(g for g in self.gates if g.directory == log.parent), self.settings)
            return 0

        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(run_suite(self.work, self.settings, REVISION, execute), 0)
        summary = json.loads((self.work / "summary.json").read_text())
        self.assertEqual(summary["status"], "PASS")
        self.assertEqual([r["validated_boots"] for r in summary["architectures"]], [3, 3, 3])
        self.assertEqual(summary["architectures"][1]["scope"], "kernel-only")

    def test_partial_boot_counts_are_rejected(self) -> None:
        gate = self.gates[0]
        campaign = fixture(gate, self.settings)
        records = json.loads((campaign / "results.json").read_text())
        write_json(campaign / "results.json", records[:-1])
        with self.assertRaisesRegex(GateError, "boot count"):
            audit(gate, self.settings, REVISION)

    def test_wrong_cpu_counts_exit_types_and_failure_flags_are_rejected(self) -> None:
        gate = self.gates[0]
        campaign = fixture(gate, self.settings)
        original = json.loads((campaign / "results.json").read_text())
        for field, value in (
            ("cpus", 4),
            ("cpus", True),
            ("exit_code", False),
            ("exit_code", 1),
            ("failure_marker", True),
            ("pass", 1),
        ):
            with self.subTest(field=field, value=value):
                records = json.loads(json.dumps(original))
                records[0][field] = value
                write_json(campaign / "results.json", records)
                with self.assertRaises(GateError):
                    audit(gate, self.settings, REVISION)

    def test_binary_tampering_is_rejected(self) -> None:
        for gate, name in zip(self.gates, ("BOOTX64.EFI", "kernel.bin", "kernel.elf"), strict=True):
            campaign = fixture(gate, self.settings)
            (campaign / name).write_bytes(b"changed")
            with self.assertRaisesRegex(GateError, "hash mismatch"):
                audit(gate, self.settings, REVISION)

    def test_stale_or_multiple_campaigns_are_rejected(self) -> None:
        gate = self.gates[0]
        campaign = fixture(gate, self.settings)
        (campaign.parent / "stale").mkdir()
        with self.assertRaisesRegex(GateError, "one fresh campaign"):
            audit(gate, self.settings, REVISION)

    def test_metadata_revision_network_and_aarch64_configuration_are_required(self) -> None:
        gate = self.gates[2]
        campaign = fixture(gate, self.settings)
        original = json.loads((campaign / "metadata.json").read_text())
        for field, value in (
            ("git_revision", "other"),
            ("network", "user"),
            ("seed", 12),
            ("rounds", 768),
            ("single_cpu_compatibility", False),
        ):
            with self.subTest(field=field):
                meta = {**original, field: value}
                write_json(campaign / "metadata.json", meta)
                with self.assertRaises(GateError):
                    audit(gate, self.settings, REVISION)

    def test_guest_fault_or_missing_serial_marker_overrides_pass_record(self) -> None:
        gate = self.gates[0]
        campaign = fixture(gate, self.settings)
        serial = campaign / "boot-0001-4cpu/serial.log"
        original = serial.read_text()
        for text in (original + "\nPANIC", original.replace("AP2: timer IRQ verified", "")):
            serial.write_text(text)
            with self.assertRaisesRegex(GateError, "serial evidence"):
                audit(gate, self.settings, REVISION)

    def test_qemu_networking_cpu_count_and_kernel_mismatch_are_rejected(self) -> None:
        gate = self.gates[1]
        campaign = fixture(gate, self.settings)
        path = campaign / "boot-0001-4cpu/command.json"
        original = json.loads(path.read_text())
        for flag, value in (
            ("-nic", "user"),
            ("-monitor", "tcp:127.0.0.1:1234"),
            ("-smp", "1"),
            ("-kernel", str(campaign / "kernel.elf")),
        ):
            with self.subTest(flag=flag):
                command = original.copy()
                command[command.index(flag) + 1] = value
                write_json(path, command)
                with self.assertRaises(GateError):
                    audit(gate, self.settings, REVISION)
        write_json(path, original + ["-netdev", "user,id=net"])
        with self.assertRaises(GateError):
            audit(gate, self.settings, REVISION)

    def test_symlinked_evidence_cannot_escape_campaign(self) -> None:
        gate = self.gates[0]
        campaign = fixture(gate, self.settings)
        serial = campaign / "boot-0000-1cpu/serial.log"
        outside = self.work / "outside.log"
        outside.write_bytes(serial.read_bytes())
        serial.unlink()
        serial.symlink_to(outside)
        with self.assertRaisesRegex(GateError, "escapes"):
            audit(gate, self.settings, REVISION)

    def test_malformed_json_or_record_shapes_are_rejected(self) -> None:
        gate = self.gates[0]
        campaign = fixture(gate, self.settings)
        path = campaign / "results.json"
        for text in ("not-json", "{}", "[1, 2, 3]"):
            path.write_text(text)
            with self.assertRaises((GateError, ValueError)):
                audit(gate, self.settings, REVISION)

    @unittest.skipUnless(os.name == "posix", "requires POSIX process groups")
    def test_stage_deadline_terminates_and_reaps_its_child(self) -> None:
        log = self.work / "sleep.log"
        command = (sys.executable, "-u", "-c", "import os,time; print(os.getpid(), flush=True); time.sleep(60)")
        with self.assertRaisesRegex(GateError, "exceeded"):
            run_child(command, log, 1)
        pid = int(log.read_text().strip())
        with self.assertRaises(ProcessLookupError):
            os.kill(pid, 0)

    @unittest.skipUnless(os.name == "posix", "requires POSIX process groups")
    def test_gate_cannot_leave_a_running_descendant_after_success_exit(self) -> None:
        log = self.work / "orphan.log"
        command = (
            sys.executable,
            "-u",
            "-c",
            "import subprocess,sys; p=subprocess.Popen([sys.executable,'-c','import time; time.sleep(60)']); print(p.pid, flush=True)",
        )
        with self.assertRaisesRegex(GateError, "descendant"):
            run_child(command, log, 5)
        pid = int(log.read_text().strip())
        for _ in range(50):
            state = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True, text=True)
            if not state.stdout.strip() or state.stdout.strip().startswith("Z"):
                break
            time.sleep(0.02)
        else:
            self.fail("child process remained running after gate cleanup")


if __name__ == "__main__":
    unittest.main()
