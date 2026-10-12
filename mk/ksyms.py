#!/usr/bin/env python3
import struct
import subprocess
import sys

MAGIC = 0x505552454B53594D
NAME_LEN = 56
CAP = 1536
AREA_SIZE = 98304


def section_info(kernel):
    out = subprocess.run(
        ["x86_64-elf-readelf", "-S", "-W", kernel],
        capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        cols = line.split()
        if len(cols) >= 7 and cols[2] == ".ksyms":
            return int(cols[4], 16), int(cols[5], 16)
    raise SystemExit("ksyms.py: no .ksyms section in %s" % kernel)


def symbol_value(kernel, name):
    out = subprocess.run(
        ["x86_64-elf-nm", kernel],
        capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        cols = line.split()
        if len(cols) == 3 and cols[2] == name:
            return int(cols[0], 16)
    raise SystemExit("ksyms.py: no symbol %s in %s" % (name, kernel))


def ksyms_offset(kernel):
    addr, off = section_info(kernel)
    area = symbol_value(kernel, "__ksyms_area")
    if area < addr:
        raise SystemExit("ksyms.py: __ksyms_area outside .ksyms")
    return off + (area - addr)


def main():
    kernel = sys.argv[1]
    entries = []
    seen = set()
    for line in sys.stdin:
        cols = line.split()
        if len(cols) != 3:
            continue
        addr, _type, name = cols
        if name.startswith(".") or name in seen:
            continue
        if len(name) > NAME_LEN - 1:
            continue
        seen.add(name)
        entries.append((name, int(addr, 16)))
    entries.sort(key=lambda item: item[0])
    if len(entries) > CAP:
        raise SystemExit("ksyms.py: %d symbols exceed cap %d"
                         % (len(entries), CAP))
    blob = struct.pack("<QQQ", MAGIC, len(entries), CAP)
    for name, addr in entries:
        blob += name.encode("utf-8") + b"\0" * (NAME_LEN - len(name))
        blob += struct.pack("<Q", addr)
    if len(blob) > AREA_SIZE:
        raise SystemExit("ksyms.py: blob too large")
    blob += b"\0" * (AREA_SIZE - len(blob))
    with open(kernel, "r+b") as handle:
        handle.seek(ksyms_offset(kernel))
        handle.write(blob)
    print("ksyms.py: %d symbols patched into %s" % (len(entries), kernel))


if __name__ == "__main__":
    main()
