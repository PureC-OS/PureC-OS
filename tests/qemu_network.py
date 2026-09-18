#!/usr/bin/env python3
"""Validate each wired PureC OS network driver in an isolated QEMU VM."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path


GATEWAY_TARGET = "10.0.2.2"
PUBLIC_PING_TARGETS = ("8.8.8.8", "1.1.1.1")
DNS_TARGETS = ("google.com", "cloudflare.com", "example.com")
FAILURE_MARKERS = (
    "[EARLY PANIC]",
    "KERNEL PANIC",
    "Kernel Panic",
    "Triple fault",
    "[NETTEST] RESULT FAIL",
)


@dataclass(frozen=True)
class Driver:
    qemu_model: str
    boot_pattern: re.Pattern[str]


DRIVERS = {
    "e1000": Driver("e1000", re.compile(r"e1000: eth0 82540EM .* link=up")),
    "8254xgc": Driver(
        "e1000-82544gc", re.compile(r"e1000: eth0 8254xGC .* link=up")
    ),
    "pcnet": Driver("pcnet", re.compile(r"pcnet: eth0 Am79C970A .* link=up")),
}


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--driver", required=True, choices=tuple(DRIVERS))
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--pcap", type=Path)
    return parser.parse_args()


def required_patterns(driver: Driver) -> list[tuple[str, re.Pattern[str]]]:
    patterns = [
        ("driver initialized", driver.boot_pattern),
        ("test device", re.compile(r"\[NETTEST\] DEVICE PASS interface=eth0")),
        ("DHCP lease", re.compile(r"\[NETTEST\] DHCP PASS interface=eth0")),
    ]
    patterns.append((
        f"ping {GATEWAY_TARGET}",
        re.compile(rf"\[NETTEST\] PING PASS target={re.escape(GATEWAY_TARGET)}\b"),
    ))
    patterns.extend(
        (
            f"public ping probe {target}",
            re.compile(
                rf"\[NETTEST\] PING (?:PASS|UNAVAILABLE) "
                rf"target={re.escape(target)}\b"
            ),
        )
        for target in PUBLIC_PING_TARGETS
    )
    patterns.extend(
        (
            f"DNS {hostname}",
            re.compile(rf"\[NETTEST\] DNS PASS host={re.escape(hostname)}\b"),
        )
        for hostname in DNS_TARGETS
    )
    patterns.append(("network result", re.compile(r"\[NETTEST\] RESULT PASS")))
    return patterns


def tail(text: str, lines: int = 100) -> str:
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


def print_egress_hint(serial: str) -> None:
    gateway_ok = f"[NETTEST] PING PASS target={GATEWAY_TARGET}" in serial
    public_missing = any(
        f"[NETTEST] PING PASS target={target}" not in serial
        and f"[NETTEST] PING UNAVAILABLE target={target}" not in serial
        for target in PUBLIC_PING_TARGETS
    )
    if gateway_ok and public_missing:
        print(
            "The guest reached the QEMU gateway, but public ICMP did not return. "
            "Check host egress and net.ipv4.ping_group_range.",
            file=sys.stderr,
        )
def qemu_supports(qemu: str, model: str) -> bool:
    result = subprocess.run(
        [qemu, "-device", "help"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    return result.returncode == 0 and re.search(
        rf'\bname "{re.escape(model)}"', result.stdout
    ) is not None


def main() -> int:
    args = arguments()
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu:
        print("qemu-system-x86_64 is not installed", file=sys.stderr)
        return 2
    if not args.iso.is_file():
        print(f"ISO does not exist: {args.iso}", file=sys.stderr)
        return 2

    driver = DRIVERS[args.driver]
    if not qemu_supports(qemu, driver.qemu_model):
        print(
            f"QEMU does not provide required NIC model: {driver.qemu_model}",
            file=sys.stderr,
        )
        return 2
    if not qemu_supports(qemu, "pci-testdev"):
        print("QEMU does not provide pci-testdev", file=sys.stderr)
        return 2

    log_path = args.log or Path(f"bin/tests/qemu-network-{args.driver}.log")
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
        "1",
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
        "-netdev",
        "user,id=net0",
        "-device",
        f"{driver.qemu_model},netdev=net0,mac=52:54:00:50:43:01",
        "-device",
        "pci-testdev",
        "-no-reboot",
    ]
    if args.pcap:
        args.pcap.parent.mkdir(parents=True, exist_ok=True)
        args.pcap.unlink(missing_ok=True)
        command.extend(
            [
                "-object",
                f"filter-dump,id=netdump,netdev=net0,file={args.pcap.resolve()}",
            ]
        )
    print(f"Testing {args.driver} ({driver.qemu_model}) with {args.iso}")
    patterns = required_patterns(driver)
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
                    print(
                        f"QEMU network test found failure marker: {failure}",
                        file=sys.stderr,
                    )
                    print(tail(serial), file=sys.stderr)
                    print_egress_hint(serial)
                    return 1

                missing = [name for name, pattern in patterns if not pattern.search(serial)]
                if not missing:
                    print(tail(serial, 35))
                    print(f"QEMU network test passed: {args.driver}")
                    return 0

                code = process.poll()
                if code is not None:
                    print(f"QEMU exited early with status {code}", file=sys.stderr)
                    print(f"Missing checks: {', '.join(missing)}", file=sys.stderr)
                    print(tail(serial), file=sys.stderr)
                    return 1
                time.sleep(0.25)

            serial = log_path.read_text(errors="replace") if log_path.exists() else ""
            missing = [name for name, pattern in patterns if not pattern.search(serial)]
            print(
                f"QEMU network test timed out after {args.timeout}s; "
                f"missing: {', '.join(missing)}",
                file=sys.stderr,
            )
            print(tail(serial), file=sys.stderr)
            print_egress_hint(serial)
            return 1
        finally:
            stop(process)


if __name__ == "__main__":
    raise SystemExit(main())