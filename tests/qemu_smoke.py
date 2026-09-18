#!/usr/bin/env python3
"""Boot a PureC OS ISO in QEMU and validate its serial boot milestones."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


FAILURE_MARKERS = (
    "[EARLY PANIC]",
    "KERNEL PANIC",
    "Kernel Panic",
    "smp selftest",
    "=FAIL",
    "Triple fault",
)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--cpus", type=int, choices=(1, 2, 4, 6, 8, 16), default=1)
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--log", type=Path)
    return parser.parse_args()


def required_patterns(cpus: int) -> list[tuple[str, re.Pattern[str]]]:
    patterns = [
        (
            "CPU topology",
            re.compile(
                rf"smp: detected={cpus} registered={cpus} online=1 source=Limine"
            ),
        ),
        ("scheduler start", re.compile(r"\[SCHED\] start")),
    ]
    patterns.append(("parallel scheduler", re.compile(
        rf"sched: {cpus} CPUs online; enabling parallel scheduling")))
    patterns.append(("SMP stress test", re.compile(
        rf"smp-test: PASS participants=0x{(1 << cpus)-1:x} allocations={cpus*8}\b")))
    for cpu in range(1, cpus):
        patterns.append((f"CPU {cpu} ready", re.compile(
            rf"smp: cpu{cpu} lapic=\d+ ready \(HLT\)")))
    return patterns


def tail(text: str, lines: int = 80) -> str:
    return "\n".join(text.splitlines()[-lines:])


def stop(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def main() -> int:
    args = arguments()
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu:
        print("qemu-system-x86_64 is not installed", file=sys.stderr)
        return 2
    if not args.iso.is_file():
        print(f"ISO does not exist: {args.iso}", file=sys.stderr)
        return 2

    log_path = args.log or Path(f"bin/tests/qemu-{args.cpus}-cpu.log")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.unlink(missing_ok=True)
    qemu_log = log_path.with_suffix(".qemu.log")

    command = [
        qemu,
        "-accel",
        "tcg",
        "-m",
        "512M",
        "-smp",
        str(args.cpus),
        "-cdrom",
        str(args.iso.resolve()),
        "-boot",
        "d",
        "-display",
        "none",
        "-monitor",
        "none",
        "-serial",
        f"file:{log_path.resolve()}",
        "-nic",
        "none",
        "-no-reboot",
    ]
    print(f"Booting {args.iso} with {args.cpus} CPU(s)")
    patterns = required_patterns(args.cpus)
    deadline = time.monotonic() + args.timeout

    with qemu_log.open("wb") as error_stream:
        process = subprocess.Popen(
            command, stdout=subprocess.DEVNULL, stderr=error_stream
        )
        try:
            while time.monotonic() < deadline:
                serial = log_path.read_text(errors="replace") if log_path.exists() else ""
                failure = next(
                    (marker for marker in FAILURE_MARKERS if marker in serial), None
                )
                if failure:
                    print(f"QEMU smoke test found failure marker: {failure}", file=sys.stderr)
                    print(tail(serial), file=sys.stderr)
                    return 1

                missing = [name for name, pattern in patterns if not pattern.search(serial)]
                if not missing:
                    print(tail(serial, 30))
                    print(f"QEMU smoke test passed with {args.cpus} CPU(s)")
                    return 0

                code = process.poll()
                if code is not None:
                    print(f"QEMU exited early with status {code}", file=sys.stderr)
                    print(f"Missing milestones: {', '.join(missing)}", file=sys.stderr)
                    print(tail(serial), file=sys.stderr)
                    return 1
                time.sleep(0.25)

            serial = log_path.read_text(errors="replace") if log_path.exists() else ""
            missing = [name for name, pattern in patterns if not pattern.search(serial)]
            print(
                f"QEMU smoke test timed out after {args.timeout}s; "
                f"missing: {', '.join(missing)}",
                file=sys.stderr,
            )
            print(tail(serial), file=sys.stderr)
            return 1
        finally:
            stop(process)


if __name__ == "__main__":
    raise SystemExit(main())
