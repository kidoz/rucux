#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run all native SMP gates offline and independently audit their retained evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import signal
import subprocess
import sys
from collections.abc import Callable
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import cast

import test_amd64_smp_smoke as amd64
import test_armv7_smp_smoke as armv7
import test_retirement_smoke as aarch64

ROOT = Path(__file__).resolve().parent.parent
ARCHITECTURES = ("amd64", "armv7", "aarch64")


@dataclass(frozen=True)
class Settings:
    attempts: int = 5
    timeout: float = 30
    stage_timeout: float = 900
    seed: int = 37
    rounds: int = 768
    uefi_code: Path = Path("/opt/homebrew/share/qemu/edk2-x86_64-code.fd")
    uefi_vars: Path = Path("/opt/homebrew/share/qemu/edk2-i386-vars.fd")


@dataclass(frozen=True)
class Gate:
    arch: str
    directory: Path
    command: tuple[str, ...]


class GateError(RuntimeError):
    pass


def write_json(path: Path, value: object) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def inside(path: Path, parent: Path) -> Path:
    resolved = path.resolve()
    if not resolved.is_relative_to(parent.resolve()):
        raise GateError(f"evidence path escapes its directory: {path.name}")
    return resolved


def read_json(path: Path, parent: Path) -> object:
    return cast(object, json.loads(inside(path, parent).read_text()))


def mapping(value: object) -> dict[str, object]:
    if not isinstance(value, dict) or any(not isinstance(key, str) for key in value):
        raise GateError("expected a JSON object")
    return cast(dict[str, object], value)


def plans(work: Path, settings: Settings) -> list[Gate]:
    result = []
    for arch in ARCHITECTURES:
        directory = work / arch
        script = {
            "amd64": "test_amd64_smp_smoke.py",
            "armv7": "test_armv7_smp_smoke.py",
            "aarch64": "test_retirement_smoke.py",
        }[arch]
        command = [
            sys.executable,
            "-u",
            str(ROOT / "tools" / script),
            "--build-dir",
            str(work / "builds" / arch),
            "--output-dir",
            str(directory / "campaigns"),
            "--attempts",
            str(settings.attempts),
            "--timeout",
            str(settings.timeout),
        ]
        if arch == "amd64":
            command += [
                "--uefi-code",
                str(settings.uefi_code.resolve()),
                "--uefi-vars",
                str(settings.uefi_vars.resolve()),
            ]
        elif arch == "aarch64":
            command += ["--with-single-cpu", "--seed", str(settings.seed), "--rounds", str(settings.rounds)]
        result.append(Gate(arch, directory, tuple(command)))
    return result


def group_exists(pid: int) -> bool:
    try:
        os.killpg(pid, 0)
    except ProcessLookupError:
        return False
    # macOS can retain an orphan zombie in the group and reject signals to it
    # with EPERM. Zombies are already stopped and cannot be reaped by this parent.
    states = subprocess.check_output(["ps", "-eo", "pgid=,stat="], text=True)
    return any(
        fields[0] == str(pid) and not fields[1].startswith("Z")
        for line in states.splitlines()
        if len(fields := line.split()) == 2
    )


def signal_group(pid: int, action: signal.Signals) -> None:
    try:
        os.killpg(pid, action)
    except ProcessLookupError:
        pass
    except PermissionError:
        if group_exists(pid):
            raise


def stop_group(process: subprocess.Popen[bytes]) -> None:
    # Only signal the private session created for this child, including QEMU.
    signal_group(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        pass
    if group_exists(process.pid):
        signal_group(process.pid, signal.SIGKILL)
    process.wait(timeout=3)


def run_child(command: tuple[str, ...], log: Path, timeout: float) -> int:
    with log.open("wb") as output:
        process = subprocess.Popen(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = process.wait(timeout=timeout)
            if group_exists(process.pid):
                raise GateError("gate left descendant processes after exit")
            return code
        except subprocess.TimeoutExpired as error:
            raise GateError(f"architecture stage exceeded {timeout:g} seconds") from error
        finally:
            stop_group(process)


def verify_hash(campaign: Path, metadata: dict[str, object], name: str, field: str) -> None:
    artifact = inside(campaign / name, campaign)
    if not artifact.is_file() or not artifact.stat().st_size:
        raise GateError(f"missing or empty {name}")
    if hashlib.sha256(artifact.read_bytes()).hexdigest() != metadata.get(field):
        raise GateError(f"hash mismatch for {name}")


def option(command: list[str], name: str) -> str:
    positions = [index for index, value in enumerate(command) if value == name]
    if len(positions) != 1 or positions[0] + 1 == len(command):
        raise GateError(f"missing or duplicate QEMU option {name}")
    return command[positions[0] + 1]


def verify_command(value: object, campaign: Path, boot: Path, arch: str, cpus: int) -> None:
    if not isinstance(value, list) or not value or any(not isinstance(item, str) for item in value):
        raise GateError("invalid QEMU command record")
    command = cast(list[str], value)
    binary = {"amd64": "qemu-system-x86_64", "armv7": "qemu-system-arm", "aarch64": "qemu-system-aarch64"}[arch]
    if Path(command[0]).name != binary or option(command, "-smp") != str(cpus):
        raise GateError("QEMU architecture or CPU count mismatch")
    if option(command, "-nic") != "none" or option(command, "-monitor") != "stdio":
        raise GateError("QEMU command is not an offline stdin-monitor gate")
    if any(flag in command for flag in ("-net", "-netdev", "-gdb", "-s", "-qmp", "-chardev")):
        raise GateError("unexpected QEMU networking/debugger channel")
    if option(command, "-serial") != f"file:{boot / 'serial.log'}":
        raise GateError("serial log does not belong to this boot")
    if arch != "amd64":
        name = "kernel.bin" if arch == "armv7" else "kernel.elf"
        if Path(option(command, "-kernel")).resolve() != campaign / name:
            raise GateError("QEMU did not use the archived kernel")
    elif f"file={campaign / 'uefi.img'},if=ide,format=raw,snapshot=on" not in command:
        raise GateError("QEMU did not use the archived snapshot image")


def audit(gate: Gate, settings: Settings, revision: str) -> tuple[Path, int]:
    output = inside(gate.directory / "campaigns", gate.directory)
    campaigns = [inside(path, output) for path in output.iterdir() if path.is_dir()]
    if len(campaigns) != 1:
        raise GateError(f"expected one fresh campaign, found {len(campaigns)}")
    campaign = campaigns[0]
    metadata = mapping(read_json(campaign / "metadata.json", campaign))
    if metadata.get("network") != "-nic none" or metadata.get("git_revision") != revision:
        raise GateError("campaign network policy or Git revision mismatch")
    verify_hash(campaign, metadata, "kernel.elf", "kernel_sha256")
    if gate.arch == "amd64":
        verify_hash(campaign, metadata, "BOOTX64.EFI", "loader_sha256")
        if not inside(campaign / "uefi.img", campaign).is_file():
            raise GateError("missing UEFI image")
    elif gate.arch == "armv7":
        verify_hash(campaign, metadata, "kernel.bin", "binary_sha256")
    else:
        if (
            type(metadata.get("seed")) is not int
            or metadata.get("seed") != settings.seed
            or type(metadata.get("rounds")) is not int
            or metadata.get("rounds") != settings.rounds
            or metadata.get("single_cpu_compatibility") is not True
        ):
            raise GateError("AArch64 seed, rounds or compatibility configuration mismatch")
    results = read_json(campaign / "results.json", campaign)
    if not isinstance(results, list) or len(results) != settings.attempts + 1:
        raise GateError("incomplete compatibility/SMP boot count")
    for index, cpus in enumerate([1] + [4] * settings.attempts):
        record = mapping(results[index])
        if type(record.get("cpus")) is not int or record.get("cpus") != cpus:
            raise GateError(f"boot {index} has the wrong CPU count")
        if (
            type(record.get("exit_code")) is not int
            or record.get("exit_code") != 0
            or record.get("failure_marker") is not False
        ):
            raise GateError(f"boot {index} did not exit cleanly or reported a failure")
        passed = record.get("result") == "PASS" if gate.arch == "aarch64" else record.get("pass") is True
        if not passed:
            raise GateError(f"boot {index} did not pass")
        name = f"attempt-{index + 1:04d}" if gate.arch == "aarch64" else f"boot-{index:04d}-{cpus}cpu"
        boot = inside(campaign / name, campaign)
        command = read_json(boot / "command.json", campaign)
        verify_command(command, campaign, boot, gate.arch, cpus)
        text = inside(boot / "serial.log", campaign).read_text(errors="replace")
        if gate.arch == "aarch64":
            ok = aarch64.accepted(text, settings.seed, settings.rounds, 0, cpus)
        elif gate.arch == "amd64":
            ok = amd64.accepted(text, cpus, 0)
        else:
            ok = armv7.accepted(text, cpus, 0)
        if not ok:
            raise GateError(f"boot {index} serial evidence is incomplete or faulting")
    return campaign, len(results)


Executor = Callable[[tuple[str, ...], Path, float], int]


def run_suite(work: Path, settings: Settings, revision: str, execute: Executor = run_child) -> int:
    gates = plans(work, settings)
    rows: list[dict[str, object]] = [
        {
            "arch": g.arch,
            "scope": "kernel-only" if g.arch == "armv7" else "userspace",
            "status": "NOT_RUN",
            "exit_code": None,
            "validated_boots": 0,
            "campaign": None,
            "error": None,
            "command": list(g.command),
            "log": str((g.directory / "gate.log").relative_to(work)),
        }
        for g in gates
    ]
    report: dict[str, object] = {
        "schema": 1,
        "status": "RUNNING",
        "git_revision": revision,
        "options": {
            "smp_attempts_per_arch": settings.attempts,
            "guest_timeout_seconds": settings.timeout,
            "stage_timeout_seconds": settings.stage_timeout,
            "aarch64_seed": settings.seed,
            "aarch64_rounds": settings.rounds,
            "network": "-nic none",
        },
        "architectures": rows,
    }
    summary = work / "summary.json"
    write_json(summary, report)
    for gate, row in zip(gates, rows, strict=True):
        row["status"] = "RUNNING"
        write_json(summary, report)
        print(f"native-smp: running {gate.arch}; {gate.directory / 'gate.log'}", flush=True)
        try:
            gate.directory.mkdir()
            write_json(gate.directory / "command.json", list(gate.command))
            code = execute(gate.command, gate.directory / "gate.log", settings.stage_timeout)
            row["exit_code"] = code
            if code:
                raise GateError(f"gate process exited with status {code}; inspect {row['log']}")
            campaign, boots = audit(gate, settings, revision)
            row["campaign"] = str(campaign.relative_to(work))
            row["validated_boots"] = boots
            row["status"] = "PASS"
        except KeyboardInterrupt:
            row["status"] = "INTERRUPTED"
            row["error"] = "interrupted; remaining architectures were not run"
            report["status"] = "INTERRUPTED"
            write_json(summary, report)
            print(f"native-smp: INTERRUPTED; {summary}", flush=True)
            return 130
        except (GateError, OSError, ValueError, subprocess.SubprocessError) as error:
            row["status"] = "FAIL"
            row["error"] = str(error)
        write_json(summary, report)
        print(f"native-smp: {gate.arch} {row['status']}" + (f" ({row['error']})" if row["error"] else ""), flush=True)
    passed = all(row["status"] == "PASS" for row in rows)
    report["status"] = "PASS" if passed else "FAIL"
    write_json(summary, report)
    print(f"native-smp: {report['status']}; {summary}", flush=True)
    return 0 if passed else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("build/native-smp-evidence"))
    parser.add_argument(
        "--attempts", type=int, default=5, help="Four-CPU boots per architecture, each after one compatibility boot"
    )
    parser.add_argument("--timeout", type=float, default=30, help="Per-guest timeout in seconds")
    parser.add_argument(
        "--stage-timeout", type=float, default=900, help="Wall-clock limit per architecture including builds"
    )
    parser.add_argument("--aarch64-seed", type=int, default=37)
    parser.add_argument("--aarch64-rounds", type=int, default=768)
    parser.add_argument("--uefi-code", type=Path, default=Settings.uefi_code)
    parser.add_argument("--uefi-vars", type=Path, default=Settings.uefi_vars)
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("native SMP process-group cleanup requires a POSIX host")
    if not (
        1 <= args.attempts <= 10000
        and math.isfinite(args.timeout)
        and 0 < args.timeout <= 300
        and math.isfinite(args.stage_timeout)
        and 0 < args.stage_timeout <= 86400
        and 0 <= args.aarch64_seed <= 2147483647
        and 3 <= args.aarch64_rounds <= 4096
    ):
        parser.error("attempts, timeouts or AArch64 workload outside supported bounds")
    try:
        output = aarch64.workspace_build(args.output_dir)
    except ValueError as error:
        parser.error(str(error))
    work = output / datetime.now(UTC).strftime("%Y%m%dT%H%M%S.%fZ")
    work.mkdir(parents=True, exist_ok=False)
    settings = Settings(
        args.attempts,
        args.timeout,
        args.stage_timeout,
        args.aarch64_seed,
        args.aarch64_rounds,
        args.uefi_code,
        args.uefi_vars,
    )
    try:
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        return run_suite(work, settings, revision)
    except (OSError, ValueError) as error:
        print(f"native-smp: cannot retain campaign evidence: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
