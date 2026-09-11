#!/usr/bin/env python3
"""Generate install assets from assets/manifest.txt (single source of truth).

Reads the 5-column manifest (module dest alias required stage) and produces:
  <staged>/manifest.txt   installer-view manifest, shipped as /manifest.txt
  <staged>/limine.modules "module_path: boot():<module>" lines for limine.conf
  <staged>/limine.conf    final bootloader config from limine.conf.in
and stages every module file into <iso_root><module>.

Optional entries whose stage file is missing are skipped everywhere
(no ISO file, no limine line); missing REQUIRED stages are fatal.
"""
import argparse
import os
import shutil
import sys

MANIFEST_NAME = "manifest.txt"


def parse_manifest(path):
    entries = []
    seen = set()
    with open(path, "r", encoding="utf-8") as handle:
        for lineno, raw in enumerate(handle, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            cols = line.split()
            if len(cols) != 5:
                raise SystemExit(
                    "manifest %s:%d: want 5 columns, got %d"
                    % (path, lineno, len(cols)))
            module, dest, alias, required, stage = cols
            if not module.startswith("/") or not dest.startswith("/"):
                raise SystemExit(
                    "manifest %s:%d: module/dest must be absolute" % (path, lineno))
            if alias != "-" and not alias.startswith("/"):
                raise SystemExit(
                    "manifest %s:%d: alias must be - or absolute" % (path, lineno))
            if required not in ("0", "1"):
                raise SystemExit(
                    "manifest %s:%d: required must be 0 or 1" % (path, lineno))
            key = (module, dest)
            if key in seen:
                raise SystemExit(
                    "manifest %s:%d: duplicate entry %s -> %s" % (path, lineno, module, dest))
            seen.add(key)
            entries.append({
                "module": module,
                "dest": dest,
                "alias": None if alias == "-" else alias,
                "required": required == "1",
                "stage": stage,
            })
    return entries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--iso", required=True)
    parser.add_argument("--staged", required=True)
    args = parser.parse_args()

    manifest_path = os.path.join(args.root, "assets", MANIFEST_NAME)
    entries = parse_manifest(manifest_path)

    os.makedirs(args.staged, exist_ok=True)

    # Stage module files, deduped by module path.
    staged_modules = {}
    for entry in entries:
        module = entry["module"]
        if module in staged_modules:
            continue
        src = os.path.join(args.root, entry["stage"])
        if not os.path.isfile(src):
            if entry["required"]:
                raise SystemExit("missing REQUIRED stage file: %s" % src)
            print("gen_assets: skip missing optional %s" % module)
            continue
        dst = os.path.join(args.iso, module.lstrip("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
        staged_modules[module] = True

    # Installer-view manifest module (/manifest.txt serves itself).
    staged_manifest = os.path.join(args.staged, MANIFEST_NAME)
    shutil.copyfile(manifest_path, staged_manifest)
    iso_manifest = os.path.join(args.iso, MANIFEST_NAME)
    shutil.copyfile(manifest_path, iso_manifest)
    staged_modules["/" + MANIFEST_NAME] = True

    # Limine module lines (manifest order, deduped).
    modules_path = os.path.join(args.staged, "limine.modules")
    with open(modules_path, "w", encoding="utf-8") as handle:
        for module in staged_modules:
            handle.write("    module_path: boot():%s\n" % module)

    # Final limine.conf from template.
    template_path = os.path.join(args.root, "src", "boot", "limine.conf.in")
    with open(template_path, "r", encoding="utf-8") as handle:
        template = handle.read()
    with open(modules_path, "r", encoding="utf-8") as handle:
        block = handle.read().rstrip("\n")
    if "@MODULES@" not in template:
        raise SystemExit("limine.conf.in has no @MODULES@ placeholder")
    final_conf = os.path.join(args.staged, "limine.conf")
    with open(final_conf, "w", encoding="utf-8") as handle:
        handle.write(template.replace("@MODULES@", block))

    print("gen_assets: %d entries, %d modules staged"
          % (len(entries), len(staged_modules)))


if __name__ == "__main__":
    main()
