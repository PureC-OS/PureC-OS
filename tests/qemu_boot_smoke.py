#!/usr/bin/env python3
import argparse
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REQUIRED = [
    ("process table", re.compile(r"process: dynamic table ready")),
    ("init as PID 1", re.compile(r"process: /bin/init started as PID 1")),
    ("scheduler start", re.compile(r"\[SCHED\] start")),
]

FAILURE = (
    "[EARLY PANIC]",
    "KERNEL PANIC",
    "Kernel Panic",
    "Triple fault",
)


def main() -> int:
    args_parser = argparse.ArgumentParser()
    args_parser.add_argument("--iso", required=True, type=Path)
    args_parser.add_argument("--timeout", type=int, default=240)
    args_parser.add_argument("--log", type=Path)
    args = args_parser.parse_args()

    qemu = shutil.which("qemu-system-x86_64")
    if not qemu:
        print("qemu-system-x86_64 is not installed", file=sys.stderr)
        return 2
    if not args.iso.is_file():
        print(f"ISO does not exist: {args.iso}", file=sys.stderr)
        return 2

    log_path = args.log or Path("bin/tests/qemu-boot-smoke.log")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    if log_path.exists():
        log_path.unlink()

    command = [
        qemu, "-accel", "tcg", "-m", "512M", "-smp", "2",
        "-cdrom", str(args.iso.resolve()), "-boot", "d",
        "-display", "none", "-monitor", "none",
        "-serial", f"file:{log_path.resolve()}",
        "-nic", "none",
    ]
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    try:
        deadline = time.time() + args.timeout
        found = {name: False for name, _ in REQUIRED}
        while time.time() < deadline:
            if process.poll() is not None:
                break
            text = log_path.read_text(errors="replace") if log_path.exists() else ""
            for marker in FAILURE:
                if marker in text:
                    print(f"FAIL: found {marker!r}")
                    print(text[-3000:])
                    return 1
            done = True
            for name, pattern in REQUIRED:
                if not found[name] and pattern.search(text):
                    found[name] = True
                    print(f"ok: {name}")
                done = done and found[name]
            if done:
                print("BOOT SMOKE PASS")
                return 0
            time.sleep(2)
        text = log_path.read_text(errors="replace") if log_path.exists() else ""
        print("FAIL: timeout waiting for boot markers")
        print(text[-3000:])
        return 1
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


if __name__ == "__main__":
    sys.exit(main())
