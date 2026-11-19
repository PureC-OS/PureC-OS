#!/usr/bin/env python3
import argparse
import hashlib
import os
import subprocess
import shutil
import sys

MANIFEST_NAME = "manifest.txt"


def cpio_newc_entry(name, data, ino=0, nlink=1):
    encoded_name = name.encode("utf-8") + b"\0"
    header = ("070701" + "%08x" % ino + "%08x" % 0o100644 +
              "%08x" % 0 + "%08x" % 0 + "%08x" % nlink + "%08x" % 0 +
              "%08x" % len(data) + "%08x" % 0 + "%08x" % 0 +
              "%08x" % 0 + "%08x" % 0 + "%08x" % len(encoded_name) +
              "%08x" % 0).encode("ascii")
    result = header + encoded_name
    result += b"\0" * ((-len(result)) % 4)
    result += data
    result += b"\0" * ((-len(result)) % 4)
    return result


def find_strip():
    for tool in (os.environ.get("STRIP"), "x86_64-elf-strip", "strip"):
        if tool and shutil.which(tool):
            return tool
    return None


def strippable(name, data):
    if data[:4] != b"\x7fELF":
        return False
    if name.startswith("/lib/modules/") or name.endswith((".o", ".a", ".ko")):
        return False
    return True


def stripped(tool, source, data, cache_dir):
    os.makedirs(cache_dir, exist_ok=True)
    key = hashlib.sha1(data).hexdigest()
    out = os.path.join(cache_dir, key)
    if not os.path.isfile(out):
        tmp = out + ".tmp"
        try:
            subprocess.run([tool, "--strip-debug", "-o", tmp, source],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            os.replace(tmp, out)
        except (OSError, subprocess.CalledProcessError):
            if os.path.exists(tmp):
                os.remove(tmp)
            return data
    with open(out, "rb") as handle:
        return handle.read()


def write_initramfs(path, files, cache_dir):
    tool = find_strip()
    contents = {}
    for name, source in files.items():
        with open(source, "rb") as input_file:
            data = input_file.read()
        if tool and strippable(name, data):
            data = stripped(tool, source, data, cache_dir)
        contents[name] = data
    groups = {}
    for name, data in contents.items():
        groups.setdefault(hashlib.sha1(data).digest(), []).append(name)
    inode = {}
    links = {}
    next_ino = 1
    for names in groups.values():
        if len(names) > 1 and contents[names[0]]:
            for n in names:
                inode[n] = next_ino
                links[n] = len(names)
            next_ino += 1
    written = set()
    saved = 0
    with open(path, "wb") as handle:
        for name, data in contents.items():
            ino = inode.get(name, 0)
            if ino and ino in written:
                handle.write(cpio_newc_entry(name.lstrip("/"), b"", ino, links[name]))
                saved += len(data)
                continue
            if ino:
                written.add(ino)
            handle.write(cpio_newc_entry(name.lstrip("/"), data, ino, links.get(name, 1)))
        handle.write(cpio_newc_entry("TRAILER!!!", b""))
    return saved


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


BOOT_MODULES = ("ext2", "crypt", "acpi")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--iso", required=True)
    parser.add_argument("--staged", required=True)
    parser.add_argument("--modules")
    parser.add_argument("--stage")
    parser.add_argument("--overlay")
    parser.add_argument("--compact", action="store_true")
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
    with open(manifest_path, "r", encoding="utf-8") as handle:
        manifest_text = handle.read()
    if args.stage:
        generated = ["", "# --- generated from the package staging root: %s ---" % args.stage]
        known = {entry["module"] for entry in entries}
        for base, dirs, names in os.walk(args.stage):
            dirs.sort()
            for name in sorted(names):
                full = os.path.join(base, name)
                module = "/" + os.path.relpath(full, args.stage).replace(os.sep, "/")
                if module in known:
                    raise SystemExit("staging file %s duplicates a manifest entry" % module)
                known.add(module)
                stage_rel = os.path.relpath(full, args.root)
                generated.append("%s %s - 1 %s" % (module, module, "-" if args.compact else stage_rel))
                initramfs_files[module] = full
                dst = os.path.join(args.iso, module.lstrip("/"))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copyfile(full, dst)
                staged_modules[module] = True
        manifest_text = manifest_text.rstrip("\n") + "\n" + "\n".join(generated) + "\n"
    if args.overlay:
        import re
        lines = ["", "# --- overlay: %s ---" % args.overlay]
        for base, dirs, names in os.walk(args.overlay):
            dirs.sort()
            for name in sorted(names):
                full = os.path.join(base, name)
                module = "/" + os.path.relpath(full, args.overlay).replace(os.sep, "/")
                stage_rel = "-" if args.compact else os.path.relpath(full, args.root)
                pattern = re.compile(r"^%s\s+%s\s+\S+\s+[01]\s+\S+$" % (re.escape(module), re.escape(module)), re.M)
                replacement = "%s %s - 0 %s" % (module, module, stage_rel)
                if module in initramfs_files:
                    manifest_text, count = pattern.subn(replacement, manifest_text)
                    if not count:
                        raise SystemExit("overlay: cannot replace manifest entry %s" % module)
                else:
                    lines.append("%s %s - 1 %s" % (module, module, stage_rel))
                initramfs_files[module] = full
                dst = os.path.join(args.iso, module.lstrip("/"))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copyfile(full, dst)
                staged_modules[module] = True
        if len(lines) > 2:
            manifest_text = manifest_text.rstrip("\n") + "\n" + "\n".join(lines) + "\n"
    staged_manifest = os.path.join(args.staged, MANIFEST_NAME)
    with open(staged_manifest, "w", encoding="utf-8") as handle:
        handle.write(manifest_text)
    iso_manifest = os.path.join(args.iso, MANIFEST_NAME)
    shutil.copyfile(staged_manifest, iso_manifest)
    staged_modules["/" + MANIFEST_NAME] = True
    initramfs_files["/" + MANIFEST_NAME] = staged_manifest

    initramfs_path = os.path.join(args.staged, "initramfs.cpio")
    linked = write_initramfs(initramfs_path, initramfs_files,
                             os.path.join(args.staged, "strip-cache"))
    iso_initramfs = os.path.join(args.iso, "boot", "initramfs.cpio")
    os.makedirs(os.path.dirname(iso_initramfs), exist_ok=True)
    shutil.copyfile(initramfs_path, iso_initramfs)

    modules_path = os.path.join(args.staged, "limine.modules")
    with open(modules_path, "w", encoding="utf-8") as handle:
        handle.write("    module_path: boot():/boot/initramfs.cpio\n")
        handle.write("    module_path: boot():/boot/esp.img\n")
        if args.modules:
            for name in BOOT_MODULES:
                for ext in ("elf", "ko"):
                    if os.path.isfile(os.path.join(args.modules, name + "." + ext)):
                        handle.write("    module_path: boot():/lib/modules/%s.%s\n" % (name, ext))

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

    print("gen_assets: %d entries, %d files in initramfs (%d KiB, %d KiB saved by hard links)"
          % (len(entries), len(initramfs_files), os.path.getsize(initramfs_path) // 1024,
             linked // 1024))


if __name__ == "__main__":
    main()
