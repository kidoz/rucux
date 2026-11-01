#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run seeded AArch64 retirement campaigns and preserve evidence before cleanup."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import time
from datetime import UTC, datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def required_markers(seed: int, rounds: int) -> tuple[str, ...]:
    return (
        f"retirement config: seed {seed} rounds {rounds} version 1",
        "rucux (aarch64) boot complete, 4 CPUs online",
        "SMP retirement: thread identity verified",
        "SMP retirement: held outgoing stack and single wait status verified",
        f"SMP retirement: PASS (4 CPUs, mask 15, {rounds} IPC/exit/cancel/wait rounds)",
        "shared mm: userspace map/protect/reuse/clone PASS",
        "shared mm: root teardown PASS (CPU mask 15)",
        "syscall safety: PASS",
    )


def failure_marker(text: str) -> bool:
    return any(
        marker in text
        for marker in (
            "SMP retirement: check",
            "SMP retirement: FAIL",
            "shared mm: FAIL",
            "shared mm: root teardown FAIL",
            "syscall safety: FAIL",
            "*** EL0",
            "AArch64 synchronous fault",
            "AArch64 IRQ fault",
            "PANIC",
        )
    )


def accepted(text: str, seed: int, rounds: int, exit_code: int | None) -> bool:
    return (
        exit_code == 0 and not failure_marker(text) and all(marker in text for marker in required_markers(seed, rounds))
    )


def workspace_build(path: Path) -> Path:
    resolved = (ROOT / path).resolve()
    if not resolved.is_relative_to(ROOT / "build"):
        raise ValueError("build/artifact paths must stay under the workspace's build directory")
    return resolved


def run_attempt(
    kernel: Path, work: Path, qemu: str, seed: int, rounds: int, threads: str, timeout: float, icount: int | None
) -> dict[str, object]:
    serial = work / "serial.log"
    monitor = work / "monitor.log"
    command = [
        qemu,
        "-accel",
        f"tcg,thread={threads}",
        "-M",
        "virt",
        "-cpu",
        "cortex-a53",
        "-smp",
        "4",
        "-m",
        "512M",
        "-kernel",
        str(kernel),
        "-serial",
        f"file:{serial}",
        "-monitor",
        "stdio",
        "-display",
        "none",
        "-nic",
        "none",
        "-no-reboot",
    ]
    if icount is not None:
        command += ["-icount", f"shift={icount},align=off,sleep=off"]
    (work / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    started = time.monotonic()
    markers = required_markers(seed, rounds)
    text = ""
    passed = False
    with monitor.open("w") as output:
        process = subprocess.Popen(
            command, cwd=ROOT, stdin=subprocess.PIPE, stdout=output, stderr=subprocess.STDOUT, text=True
        )
        try:
            while time.monotonic() - started < timeout:
                text = serial.read_text() if serial.exists() else ""
                if failure_marker(text):
                    # The failure path freezes the event rings before printing.
                    # Wait for the bounded dump, preserving the controller TCB.
                    if "retirement diagnostics: end" not in text and process.poll() is None:
                        time.sleep(0.02)
                        continue
                    break
                if all(marker in text for marker in markers):
                    passed = True
                    break
                if process.poll() is not None:
                    break
                time.sleep(0.02)
            if process.poll() is None and process.stdin is not None:
                if not passed:
                    process.stdin.write(
                        "stop\ninfo cpus\ncpu 0\ninfo registers\ncpu 1\ninfo registers\n"
                        "cpu 2\ninfo registers\ncpu 3\ninfo registers\n"
                    )
                    process.stdin.flush()
                    time.sleep(0.15)
                    # Fixed virt/512 MiB guest: retain bounded kernel metadata
                    # and upper allocator RAM, including event rings and TCBs.
                    # No host memory, sockets or debugger ports are exposed.
                    for name, address in (("kernel-memory.bin", 0x40000000), ("heap-memory.bin", 0x5F000000)):
                        target = work / name
                        process.stdin.write(f'pmemsave {address:#x} 16777216 "{target}"\n')
                        process.stdin.flush()
                        limit = time.monotonic() + 3
                        while time.monotonic() < limit:
                            if target.exists() and target.stat().st_size == 16777216:
                                break
                            time.sleep(0.02)
                process.stdin.write("quit\n")
                process.stdin.flush()
                process.wait(timeout=3)
        except BrokenPipeError:
            passed = False
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    text = serial.read_text() if serial.exists() else ""
    passed = passed and accepted(text, seed, rounds, process.returncode)
    return {
        "result": "PASS" if passed else "FAIL",
        "elapsed_seconds": round(time.monotonic() - started, 3),
        "exit_code": process.returncode,
        "missing_markers": [marker for marker in markers if marker not in text],
        "failure_marker": failure_marker(text),
        "diagnostics_complete": "retirement diagnostics: end" in text,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/retirement-investigation"))
    parser.add_argument("--kernel", type=Path, help="Replay an archived ELF without configuring or building")
    parser.add_argument("--output-dir", type=Path, default=Path("build/retirement-evidence"))
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--rounds", type=int, default=768)
    parser.add_argument("--attempts", type=int, default=20)
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--tcg-threads", choices=("single", "multi"), default="multi")
    parser.add_argument("--icount", type=int, choices=range(0, 11))
    args = parser.parse_args()
    if not (
        0 <= args.seed <= 2147483647
        and 3 <= args.rounds <= 4096
        and 1 <= args.attempts <= 10000
        and 0 < args.timeout <= 300
    ):
        parser.error("seed/rounds/attempts/timeout outside supported bounds")
    if args.icount is not None and args.tcg_threads != "single":
        parser.error("instruction counting requires --tcg-threads single")
    try:
        build = workspace_build(args.build_dir)
        output = workspace_build(args.output_dir)
        kernel = workspace_build(args.kernel) if args.kernel is not None else build / "kernel.elf"
    except ValueError as error:
        parser.error(str(error))
    qemu = shutil.which("qemu-system-aarch64")
    if qemu is None:
        parser.error("qemu-system-aarch64 is not installed; nothing is fetched")
    work = output / datetime.now(UTC).strftime("%Y%m%dT%H%M%S.%fZ")
    work.mkdir(parents=True, exist_ok=False)
    if args.kernel is None:
        with (work / "build.log").open("w") as log:
            setup = [
                "meson",
                "setup",
                str(build),
                "--cross-file",
                "cross_aarch64.txt",
                "-Dproduct=smoke-qemu-aarch64",
                f"-Dretirement_rounds={args.rounds}",
                f"-Dretirement_seed={args.seed}",
            ]
            if build.exists():
                setup.append("--reconfigure")
            subprocess.run(setup, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
            subprocess.run(
                ["meson", "compile", "-C", str(build)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True
            )
    archived = work / "kernel.elf"
    shutil.copyfile(kernel, archived)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    metadata = {
        "schema": 1,
        "seed": args.seed,
        "rounds": args.rounds,
        "tcg_threads": args.tcg_threads,
        "icount": args.icount,
        "timeout_seconds": args.timeout,
        "git_revision": revision,
        "kernel_sha256": hashlib.sha256(archived.read_bytes()).hexdigest(),
        "qemu_version": subprocess.check_output([qemu, "--version"], text=True).strip(),
        "host": platform.platform(),
        "network": "-nic none",
        "workload_replay_only": True,
        "failure_memory_ranges": [[0x40000000, 16777216], [0x5F000000, 16777216]],
    }
    (work / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    results = []
    for attempt in range(1, args.attempts + 1):
        attempt_dir = work / f"attempt-{attempt:04d}"
        attempt_dir.mkdir()
        result = run_attempt(
            archived, attempt_dir, qemu, args.seed, args.rounds, args.tcg_threads, args.timeout, args.icount
        )
        results.append(result)
        (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(
            f"retirement: attempt {attempt} {result['result']} ({result['elapsed_seconds']}s); {attempt_dir}",
            flush=True,
        )
        if result["result"] != "PASS":
            return 1
    print(f"retirement: PASS ({args.attempts} boots, seed {args.seed}, {args.rounds} rounds); {work}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
