#!/usr/bin/env python3
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pkglib


def add_dirs(entries, seen, dest):
    parts = dest.split("/")[:-1]
    for i in range(1, len(parts) + 1):
        d = "/".join(parts[:i])
        if d and d not in seen:
            seen.add(d)
            entries.append((d, "dir", 0o755, None))


def main():
    p = argparse.ArgumentParser(description="build a pkg package")
    p.add_argument("--name", required=True)
    p.add_argument("--version", required=True)
    p.add_argument("--desc", default="")
    p.add_argument("--arch", default="x86_64")
    p.add_argument("--depend", action="append", default=[])
    p.add_argument("--conflict", action="append", default=[])
    p.add_argument("--backup", action="append", default=[])
    p.add_argument("--hook", action="append", default=[], help="NAME=FILE with NAME in post-install, post-upgrade, pre-remove, post-remove")
    p.add_argument("--file", action="append", default=[], help="DEST=SOURCE")
    p.add_argument("--mode", action="append", default=[], help="DEST=OCTALMODE")
    p.add_argument("--link", action="append", default=[], help="DEST=TARGET")
    p.add_argument("--tree", action="append", default=[], help="DEST=SOURCEDIR")
    p.add_argument("-o", "--output", default=".")
    args = p.parse_args()

    files = []
    for spec in args.file:
        dest, src = spec.split("=", 1)
        files.append((dest.lstrip("/"), src))
    for spec in args.tree:
        dest, src = spec.split("=", 1)
        dest = dest.lstrip("/")
        for root, dirs, names in os.walk(src):
            dirs.sort()
            for name in sorted(names):
                full = os.path.join(root, name)
                rel = os.path.relpath(full, src)
                files.append(("%s/%s" % (dest, rel) if dest else rel, full))
    modes = {}
    for spec in args.mode:
        dest, value = spec.split("=", 1)
        modes[dest.lstrip("/")] = int(value, 8) & 0o7777
    entries = []
    seen = set()
    total = 0
    payload = []
    for dest, src in sorted(files):
        with open(src, "rb") as f:
            data = f.read()
        mode = modes.get(dest, 0o755 if os.access(src, os.X_OK) else 0o644)
        add_dirs(payload, seen, dest)
        payload.append((dest, "file", mode, data))
        total += len(data)
    for spec in args.link:
        dest, target = spec.split("=", 1)
        dest = dest.lstrip("/")
        add_dirs(payload, seen, dest)
        payload.append((dest, "link", 0o777, target))
    info = {"pkgname": args.name, "pkgver": args.version, "pkgdesc": args.desc, "arch": args.arch, "size": str(total),
            "depend": args.depend, "conflict": args.conflict, "backup": [b.lstrip("/") for b in args.backup]}
    entries.append((".PKGINFO", "file", 0o644, pkglib.format_info(info).encode()))
    for spec in args.hook:
        name, src = spec.split("=", 1)
        if name not in ("post-install", "post-upgrade", "pre-remove", "post-remove"):
            sys.exit("unknown hook %s" % name)
        with open(src, "rb") as f:
            entries.append((".hook-" + name, "file", 0o755, f.read()))
    entries.extend(payload)
    blob = pkglib.make_tar_gz(entries)
    os.makedirs(args.output, exist_ok=True)
    filename = "%s-%s-%s.pkg.tar.gz" % (args.name, args.version, args.arch)
    with open(os.path.join(args.output, filename), "wb") as f:
        f.write(blob)
    print(filename)


if __name__ == "__main__":
    main()
