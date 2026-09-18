#!/usr/bin/env python3
import argparse
import os
import shutil
import sys

MANIFEST_NAME = "manifest.txt"


def cpio_newc_entry(name, data):
    encoded_name = name.encode("utf-8") + b"\0"
    header = ("070701" + "%08x" % 0 + "%08x" % 0o100644 +
              "%08x" % 0 + "%08x" % 0 + "%08x" % 1 + "%08x" % 0 +
              "%08x" % len(data) + "%08x" % 0 + "%08x" % 0 +
              "%08x" % 0 + "%08x" % 0 + "%08x" % len(encoded_name) +
              "%08x" % 0).encode("ascii")
    result = header + encoded_name
    result += b"\0" * ((-len(result)) % 4)
    result += data
    result += b"\0" * ((-len(result)) % 4)
    return result


def write_initramfs(path, files):
    with open(path, "wb") as handle:
        for name, source in files.items():
            with open(source, "rb") as input_file:
                handle.write(cpio_newc_entry(name.lstrip("/"), input_file.read()))
        handle.write(cpio_newc_entry("TRAILER!!!", b""))


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
    staged_modules = {}
    initramfs_files = {}
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
        initramfs_files[module] = src
    staged_manifest = os.path.join(args.staged, MANIFEST_NAME)
    shutil.copyfile(manifest_path, staged_manifest)
    iso_manifest = os.path.join(args.iso, MANIFEST_NAME)
    shutil.copyfile(manifest_path, iso_manifest)
    staged_modules["/" + MANIFEST_NAME] = True
    initramfs_files["/" + MANIFEST_NAME] = staged_manifest

    initramfs_path = os.path.join(args.staged, "initramfs.cpio")
    write_initramfs(initramfs_path, initramfs_files)
    iso_initramfs = os.path.join(args.iso, "boot", "initramfs.cpio")
    os.makedirs(os.path.dirname(iso_initramfs), exist_ok=True)
    shutil.copyfile(initramfs_path, iso_initramfs)

    modules_path = os.path.join(args.staged, "limine.modules")
    with open(modules_path, "w", encoding="utf-8") as handle:
        handle.write("    module_path: boot():/boot/initramfs.cpio\n")

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

    print("gen_assets: %d entries, %d files in initramfs (%d KiB)"
          % (len(entries), len(initramfs_files), os.path.getsize(initramfs_path) // 1024))


if __name__ == "__main__":
    main()
