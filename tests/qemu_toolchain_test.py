#!/usr/bin/env python3
"""Boot the ISO in QEMU, run the guest toolchain self-test and read its verdict from serial.

The self-test (tests/guest/selftest.sh, shipped as /usr/share/purec/selftest.sh) builds a
C program with make and tcc inside PureC OS and reports through /dev/kmsg.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from qemu_panic_test import mon_command, read_serial, stop, tail

READY_MARKERS = ("[SCHED] start",)
COMMAND = "sh /usr/share/purec/selftest.sh"
KEY_NAMES = {"/": "slash", ".": "dot", " ": "spc", "-": "minus", "_": "shift-minus"}


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--attempts", type=int, default=3)
    parser.add_argument("--settle", type=int, default=30)
    return parser.parse_args()


def key_for(character: str) -> str:
    if character.isalnum() and character.islower() or character.isdigit():
        return character
    if character in KEY_NAMES:
        return KEY_NAMES[character]
    raise ValueError(f"no QEMU key for {character!r}")


def type_command(sock_path: str, command: str) -> None:
    for character in command:
        mon_command(sock_path, f"sendkey {key_for(character)}")
        time.sleep(0.15)
    time.sleep(0.3)
    mon_command(sock_path, "sendkey ret")


def verdict(serial: str) -> str | None:
    for line in serial.splitlines():
        if "SELFTEST PASS" in line:
            return "PASS"
        if "SELFTEST FAIL" in line:
            return line[line.index("SELFTEST FAIL"):].strip()
    return None


def main() -> int:
    args = arguments()
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu:
        print("qemu-system-x86_64 is not installed", file=sys.stderr)
        return 2
    if not args.iso.is_file():
        print(f"ISO does not exist: {args.iso}", file=sys.stderr)
        return 2

    log_path = args.log or Path("bin/tests/qemu-toolchain.log")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.unlink(missing_ok=True)
    qemu_log = log_path.with_suffix(".qemu.log")
    tmp = tempfile.mkdtemp(prefix="qemu-toolchain-")
    sock_path = str(Path(tmp) / "mon.sock")

    command = [
        qemu,
        "-accel", "tcg",
        "-m", "1024M",
        "-smp", "2",
        "-cdrom", str(args.iso.resolve()),
        "-boot", "d",
        "-display", "none",
        "-monitor", f"unix:{sock_path},server,nowait",
        "-serial", f"file:{log_path.resolve()}",
        "-nic", "none",
        "-no-reboot",
    ]
    print(f"Booting {args.iso}, will run '{COMMAND}' in the guest shell")
    deadline = time.monotonic() + args.timeout
    with qemu_log.open("wb") as error_stream:
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=error_stream)
        try:
            while time.monotonic() < deadline:
                if all(m in read_serial(log_path) for m in READY_MARKERS):
                    break
                if process.poll() is not None:
                    print(f"QEMU exited early with status {process.poll()}", file=sys.stderr)
                    return 1
                time.sleep(1.0)
            else:
                print("Boot readiness markers never appeared", file=sys.stderr)
                return 1
            print(f"Guest booted, giving the desktop {args.settle}s to settle")
            time.sleep(args.settle)

            result = None
            for attempt in range(1, args.attempts + 1):
                if process.poll() is not None:
                    print(f"QEMU exited early with status {process.poll()}", file=sys.stderr)
                    print(tail(read_serial(log_path)), file=sys.stderr)
                    return 1
                if "SELFTEST START" not in read_serial(log_path):
                    print(f"Typing the self-test command (attempt {attempt}/{args.attempts})")
                    type_command(sock_path, COMMAND)
                    start_deadline = min(time.monotonic() + 60, deadline)
                    while time.monotonic() < start_deadline:
                        if "SELFTEST START" in read_serial(log_path):
                            break
                        time.sleep(1.0)
                    else:
                        continue
                while time.monotonic() < deadline:
                    result = verdict(read_serial(log_path))
                    if result or process.poll() is not None:
                        break
                    time.sleep(2.0)
                break

            serial = read_serial(log_path)
            if "SELFTEST START" not in serial:
                print("Self-test never started; keys did not reach a shell", file=sys.stderr)
                print(tail(serial), file=sys.stderr)
                return 1
            if result is None:
                print("Self-test did not finish before the timeout", file=sys.stderr)
                print(tail(serial), file=sys.stderr)
                return 1
            if result != "PASS":
                print(f"Guest toolchain self-test failed: {result}", file=sys.stderr)
                print(tail(serial), file=sys.stderr)
                return 1
            print("Guest toolchain self-test passed (sh, make, tcc, box utilities)")
            return 0
        finally:
            stop(process)
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
