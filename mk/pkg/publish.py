#!/usr/bin/env python3
import argparse
import os
import re
import shutil
import sys

PATTERN = re.compile(r"^(?P<name>[A-Za-z0-9._+-]+)-(?P<ver>[0-9][^-]*)-(?P<rel>[^-]+)-(?P<arch>x86_64|any)\.pkg\.tar\.gz$")


def main():
    ap = argparse.ArgumentParser(description="put packages into a registry checkout")
    ap.add_argument("--registry", required=True)
    ap.add_argument("--keep", type=int, default=1)
    ap.add_argument("packages", nargs="+")
    args = ap.parse_args()
    conf_path = os.path.join(args.registry, "repository.conf")
    header, entries = [], {}
    if os.path.exists(conf_path):
        for line in open(conf_path, encoding="utf-8").read().splitlines():
            s = line.strip()
            if not s or s.startswith("#"):
                if not entries:
                    header.append(line)
                continue
            parts = s.split(None, 2)
            if len(parts) >= 2:
                entries[parts[0]] = [parts[1], parts[2] if len(parts) > 2 else ""]
    if not header:
        header = ["# PureC OS registry: NAME PATH [DESCRIPTION], PATH is relative to this file;", "# the signature is PATH.sig next to the archive."]
    for pkg in args.packages:
        base = os.path.basename(pkg)
        m = PATTERN.match(base)
        if not m:
            sys.exit(f"{base}: not NAME-VERSION-REL-ARCH.pkg.tar.gz")
        name = m.group("name")
        folder = os.path.join(args.registry, "bin", name)
        os.makedirs(folder, exist_ok=True)
        shutil.copyfile(pkg, os.path.join(folder, base))
        sig = pkg + ".sig"
        if os.path.exists(sig):
            shutil.copyfile(sig, os.path.join(folder, base + ".sig"))
        else:
            print(f"warning: {base} has no signature", file=sys.stderr)
        old = entries.get(name, ["", ""])
        entries[name] = [f"bin/{name}/{base}", old[1]]
        files = sorted(f for f in os.listdir(folder) if PATTERN.match(f))
        stale = [f for f in files if f != base]
        for f in stale[: max(0, len(stale) - (args.keep - 1))]:
            os.unlink(os.path.join(folder, f))
            if os.path.exists(os.path.join(folder, f + ".sig")):
                os.unlink(os.path.join(folder, f + ".sig"))
        print(f"{name}: bin/{name}/{base}")
    out = list(header)
    for name in sorted(entries):
        path, desc = entries[name]
        out.append(f"{name} {path}" + (f" {desc}" if desc else ""))
    open(conf_path, "w", encoding="utf-8").write("\n".join(out) + "\n")


main()
