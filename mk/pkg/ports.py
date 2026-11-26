#!/usr/bin/env python3
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)


def read(path):
    with open(os.path.join(ROOT, path), "r", errors="replace") as f:
        return f.read()


def grab(path, pattern, default):
    try:
        m = re.search(pattern, read(path), re.M)
        return m.group(1) if m else default
    except OSError:
        return default


def lua_version():
    return grab("programs/PureC-OS-Core-Programs/lua/lua.h", r'LUA_VERSION_RELEASE\s+"(\d+)"', "0").join(["5.4.", ""])


def vim_version():
    major = grab("vim/src/version.h", r"VIM_VERSION_MAJOR\s+(\d+)", "9")
    minor = grab("vim/src/version.h", r"VIM_VERSION_MINOR\s+(\d+)", "0")
    return "%s.%s" % (major, minor)


def curl_version():
    return grab("curl/include/curl/curlver.h", r'LIBCURL_VERSION "([0-9.]+)', "8.0.0")


def sdl_version():
    base = "SDL/include/SDL_version.h"
    return "%s.%s.%s" % (grab(base, r"SDL_MAJOR_VERSION\s+(\d+)", "2"), grab(base, r"SDL_MINOR_VERSION\s+(\d+)", "0"),
                         grab(base, r"SDL_PATCHLEVEL\s+(\d+)", "0"))


def wolf_version():
    return grab("wolfssl/wolfssl/version.h", r'LIBWOLFSSL_VERSION_STRING "([0-9.]+)"', "5.0.0")


def make_version():
    return grab("gnumake/configure.ac", r"AC_INIT\(\[GNU Make\],\[([0-9.]+)\]", "4.4")


def tcc_version():
    try:
        return read("tcc/VERSION").strip().replace("rc", "rc")
    except OSError:
        return "0.9.28"


def git_version():
    m = re.search(r"v?(\d+\.\d+\.\d+)", read("mk/git/versions.txt")) if os.path.exists(os.path.join(ROOT, "mk/git/versions.txt")) else None
    return m.group(1) if m else "2.0.0"


def header_tree(srcdir, destdir):
    out = []
    base = os.path.join(ROOT, srcdir)
    for root, dirs, names in os.walk(base):
        dirs.sort()
        for name in sorted(names):
            if name.endswith(".h"):
                rel = os.path.relpath(os.path.join(root, name), base)
                out.append(("%s/%s" % (destdir, rel), "%s/%s" % (srcdir, rel)))
    return out


def header_files(srcdir, destdir, pattern):
    out = []
    base = os.path.join(ROOT, srcdir)
    if not os.path.isdir(base):
        return out
    for name in sorted(os.listdir(base)):
        if re.match(pattern, name):
            out.append(("%s/%s" % (destdir, name), "%s/%s" % (srcdir, name)))
    return out


PORTS = [
    {"name": "vim", "version": vim_version, "rel": 1, "desc": "Vi IMproved text editor",
     "files": [("usr/bin/vim", "bin/programs/vim"), ("etc/vimrc", "rootfs/etc/vimrc")], "backup": ["etc/vimrc"]},
    {"name": "lua", "version": lua_version, "rel": 1, "desc": "Lua scripting language",
     "files": [("usr/bin/lua", "bin/programs/lua")]},
    {"name": "micropython", "version": lambda: "1.0.0", "rel": 1, "desc": "Python for small systems",
     "files": [("usr/bin/micropython", "bin/programs/micropython")]},
    {"name": "make", "version": make_version, "rel": 1, "desc": "GNU make",
     "files": [("usr/bin/make", "bin/programs/make")]},
    {"name": "git", "version": git_version, "rel": 1, "desc": "Distributed version control",
     "files": [("usr/bin/git", "bin/programs/git"), ("usr/libexec/git-core/git", "bin/programs/git"),
               ("usr/libexec/git-core/git-remote-http", "bin/programs/git-remote-http"),
               ("usr/libexec/git-core/git-remote-https", "bin/programs/git-remote-http"),
               ("usr/share/git-core/templates/description", "bin/git-templates/description"),
               ("usr/share/git-core/templates/info/exclude", "bin/git-templates/info/exclude")]},
    {"name": "tcc", "version": tcc_version, "rel": 1, "desc": "Tiny C compiler",
     "files": [("usr/bin/tcc", "bin/programs/tcc"), ("lib/tcc/libtcc1.a", "bin/lib/tcc/libtcc1.a")]
     + header_files("tcc/include", "lib/tcc/include", r".*\.h$")},
    {"name": "wolfssl", "version": wolf_version, "rel": 1, "desc": "wolfSSL TLS library (development files)",
     "files": [("usr/lib/libwolf.a", "bin/lib/libwolf.a"), ("usr/include/wolfssl/user_settings.h", "wolfssl/purec/user_settings.h")]
     + header_tree("wolfssl/wolfssl", "usr/include/wolfssl")},
    {"name": "sdl2", "version": sdl_version, "rel": 1, "desc": "Simple DirectMedia Layer 2 (development files)",
     "files": [("usr/lib/libSDL2.a", "bin/lib/libSDL2.a"), ("usr/lib/libSDL2_mixer.a", "bin/lib/libSDL2_mixer.a")]
     + header_files("SDL/include", "usr/include/SDL2", r".*\.h$")},
    {"name": "chocolate-doom", "version": lambda: "3.1.0", "rel": 1, "desc": "Doom source port (SDL2)",
     "depend": [], "files": [("usr/bin/chocolate-doom", "bin/programs/chocolate-doom")]},
]


def main():
    p = argparse.ArgumentParser(description="build the port packages as GitHub release assets")
    p.add_argument("output")
    p.add_argument("--name", default="ports")
    p.add_argument("--sign")
    p.add_argument("--only", action="append", default=[])
    p.add_argument("--pkgrel", type=int)
    args = p.parse_args()
    os.makedirs(args.output, exist_ok=True)
    built = 0
    for port in PORTS:
        if args.only and port["name"] not in args.only:
            continue
        files = []
        missing = []
        for dest, src in port["files"]:
            full = os.path.join(ROOT, src)
            if os.path.isfile(full):
                files.append("--file")
                files.append("%s=%s" % (dest, full))
            else:
                missing.append(src)
        if missing:
            print("skip %s: not built (%s)" % (port["name"], missing[0]), file=sys.stderr)
            continue
        cmd = [sys.executable, os.path.join(HERE, "mkpkg.py"), "--name", port["name"],
               "--version", "%s-%d" % (port["version"](), args.pkgrel or port["rel"]), "--desc", port["desc"], "-o", args.output] + files
        for dep in port.get("depend", []):
            cmd += ["--depend", dep]
        for b in port.get("backup", []):
            cmd += ["--backup", b]
        out = subprocess.run(cmd, check=True, stdout=subprocess.PIPE, text=True).stdout.strip().splitlines()[-1]
        asset = os.path.join(args.output, out)
        if args.sign:
            subprocess.run([sys.executable, os.path.join(HERE, "pkgsign.py"), "sign", args.sign, asset, asset + ".sig"],
                           check=True)
        built += 1
        print("built %s%s" % (out, " + .sig" if args.sign else ""))
    print("%d package(s) in %s; upload each NAME-VERSION-x86_64.pkg.tar.gz (and .sig) as an asset of a "
          "GitHub release in the repository listed for NAME in /etc/pkg/pkg.conf" % (built, args.output))


if __name__ == "__main__":
    main()
