#!/usr/bin/env python3
import argparse
import glob
import os
import sys
import tarfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pkglib


def read_info(path):
    with tarfile.open(path, "r:gz") as tar:
        for member in tar:
            if member.name == ".PKGINFO":
                return pkglib.parse_info(tar.extractfile(member).read().decode())
    raise SystemExit("%s has no .PKGINFO" % path)


def main():
    p = argparse.ArgumentParser(description="build a repository database from a directory of packages")
    p.add_argument("directory")
    p.add_argument("--name", default="core")
    p.add_argument("--sign", help="private key (PEM) used to sign the database")
    args = p.parse_args()

    best = {}
    for path in sorted(glob.glob(os.path.join(args.directory, "*.pkg.tar.gz"))):
        info = read_info(path)
        name = info["pkgname"]
        if name in best and pkglib.vercmp(best[name][0]["pkgver"], info["pkgver"]) >= 0:
            continue
        best[name] = (info, path)
    entries = []
    for name in sorted(best):
        info, path = best[name]
        info = dict(info)
        info["filename"] = os.path.basename(path)
        info["csize"] = str(os.path.getsize(path))
        info["sha512"] = pkglib.sha512_file(path)
        info["backup"] = []
        entries.append(("%s-%s" % (name, info["pkgver"]), "dir", 0o755, None))
        entries.append(("%s-%s/desc" % (name, info["pkgver"]), "file", 0o644, pkglib.format_info(info).encode()))
    db = os.path.join(args.directory, args.name + ".db")
    with open(db, "wb") as f:
        f.write(pkglib.make_tar_gz(entries))
    print("%s: %d package(s)" % (db, len(best)))
    sig = db + ".sig"
    if args.sign:
        import pkgsign
        pkgsign.sign_file(args.sign, db, sig)
        print(sig)
    elif os.path.exists(sig):
        os.unlink(sig)


if __name__ == "__main__":
    main()
