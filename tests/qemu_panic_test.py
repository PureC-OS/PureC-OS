#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


UUID_RE = re.compile(r"Build ([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})")
SERIAL_MARKERS = (
    "Kernel Panic",
    "Scan QR to open a prefilled GitHub issue:",
    "Call Trace",
    "RIP sym:",
    "at src/kernel/syscall/syscall.c:",
    "kernel panic test from shell",
)
ISSUE_PREFIX = "https://github.com/PureC-OS/PureC-OS/issues/new?title=Kernel%20panic%20"
READY_MARKERS = (
    "[SCHED] start",
)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--shot", type=Path)
    parser.add_argument("--attempts", type=int, default=3)
    return parser.parse_args()


def tail(text: str, lines: int = 60) -> str:
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


def mon_readout(sock: socket.socket, timeout: float = 5.0) -> bytes:
    sock.settimeout(timeout)
    data = b""
    try:
        while not data.endswith(b"(qemu) "):
            chunk = sock.recv(4096)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    return data


def mon_command(sock_path: str, command: str) -> str:
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        sock.connect(sock_path)
        mon_readout(sock)
        sock.sendall(command.encode() + b"\n")
        return mon_readout(sock).decode(errors="replace")
    finally:
        sock.close()


def decode_tools() -> list[str]:
    missing = []
    try:
        import PIL  # noqa: F401
    except ImportError:
        missing.append("pillow")
    if shutil.which("zbarimg") is None:
        missing.append("zbarimg")
    return missing


def type_panic(sock_path: str) -> None:
    for key in ("p", "a", "n", "i", "c"):
        mon_command(sock_path, f"sendkey {key}")
        time.sleep(0.2)
    time.sleep(0.3)
    mon_command(sock_path, "sendkey ret")


def decode_qr(ppm: Path, png: Path) -> str:
    from PIL import Image
    Image.open(ppm).convert("1").save(png)
    proc = subprocess.run(["zbarimg", "--raw", str(png)], capture_output=True)
    if proc.returncode != 0:
        raise SystemExit(f"zbarimg failed: {proc.stderr.decode(errors='replace')[:500]}")
    return proc.stdout.decode(errors="replace").strip()


def read_serial(log_path: Path) -> str:
    return log_path.read_text(errors="replace") if log_path.exists() else ""


def main() -> int:
    args = arguments()
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu:
        print("qemu-system-x86_64 is not installed", file=sys.stderr)
        return 2
    if not args.iso.is_file():
        print(f"ISO does not exist: {args.iso}", file=sys.stderr)
        return 2

    log_path = args.log or Path("bin/tests/qemu-panic.log")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.unlink(missing_ok=True)
    shot_path = args.shot or Path("bin/tests/qemu-panic-shot.ppm")
    shot_path.parent.mkdir(parents=True, exist_ok=True)
    if shot_path.exists():
        shot_path.unlink()
    qemu_log = log_path.with_suffix(".qemu.log")

    tmp = tempfile.mkdtemp(prefix="qemu-panic-")
    sock_path = str(Path(tmp) / "mon.sock")

    command = [
        qemu,
        "-accel", "tcg",
        "-m", "512M",
        "-smp", "1",
        "-cdrom", str(args.iso.resolve()),
        "-boot", "d",
        "-display", "none",
        "-monitor", f"unix:{sock_path},server,nowait",
        "-serial", f"file:{log_path.resolve()}",
        "-nic", "none",
        "-no-reboot",
    ]
    print(f"Booting {args.iso}, will type 'panic' into the shell via monitor")
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
            print("Guest booted, giving the desktop time to settle")
            time.sleep(30)
            panicked = False
            for attempt in range(1, args.attempts + 1):
                if process.poll() is not None:
                    print(f"QEMU exited early with status {process.poll()}", file=sys.stderr)
                    print(tail(read_serial(log_path)), file=sys.stderr)
                    return 1
                print(f"Typing 'panic' (attempt {attempt}/{args.attempts})")
                type_panic(sock_path)
                attempt_deadline = min(time.monotonic() + 40, deadline)
                while time.monotonic() < attempt_deadline:
                    serial = read_serial(log_path)
                    if "Kernel Panic" in serial and "Scan QR" in serial:
                        panicked = True
                        break
                    if process.poll() is not None:
                        break
                    time.sleep(0.5)
                if panicked:
                    break
            serial = read_serial(log_path)
            if not panicked:
                print("No panic after typing; keys did not reach a shell", file=sys.stderr)
                print(tail(serial), file=sys.stderr)
                return 1
            missing = [m for m in SERIAL_MARKERS if m not in serial]
            if missing:
                print(f"Missing serial markers: {missing}", file=sys.stderr)
                print(tail(serial), file=sys.stderr)
                return 1
            print("Serial markers ok")
            match = UUID_RE.search(serial)
            if not match:
                print("Build UUID not found in serial log", file=sys.stderr)
                return 1
            uuid = match.group(1)
            print(f"Build UUID: {uuid}")
            absent = decode_tools()
            if absent:
                print(f"QR decode tools missing ({', '.join(absent)}), serial asserts passed")
                print("QEMU panic test passed (serial only)")
                return 0
            mon_command(sock_path, f"screendump {shot_path.resolve()}")
            time.sleep(2)
            if not shot_path.is_file():
                print("screendump produced no file", file=sys.stderr)
                return 1
            url = decode_qr(shot_path, shot_path.with_suffix(".png"))
            if ISSUE_PREFIX + uuid not in url:
                print(f"QR URL does not carry panic UUID: {url[:120]}", file=sys.stderr)
                return 1
            if "&body=" not in url or len(url) < 200:
                print(f"QR URL has no panic body: {url[:120]}", file=sys.stderr)
                return 1
            print(f"QR decodes to prefilled issue for {uuid} ({len(url)} chars)")
            print("QEMU panic test passed")
            return 0
        finally:
            stop(process)
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
