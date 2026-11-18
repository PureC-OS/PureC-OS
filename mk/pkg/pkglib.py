import gzip
import hashlib
import io
import os
import re
import tarfile


def _segments(text):
    return re.findall(r"[0-9]+|[A-Za-z]+|[^0-9A-Za-z]+", text)


def _rpmvercmp(a, b):
    if a == b:
        return 0
    one, two = 0, 0
    prev1, prev2 = 0, 0
    while one < len(a) and two < len(b):
        while one < len(a) and not a[one].isalnum():
            one += 1
        while two < len(b) and not b[two].isalnum():
            two += 1
        if one >= len(a) or two >= len(b):
            break
        if (one - prev1) != (two - prev2):
            return -1 if (one - prev1) < (two - prev2) else 1
        s1, s2 = one, two
        if a[s1].isdigit():
            numeric = True
            while s1 < len(a) and a[s1].isdigit():
                s1 += 1
            while s2 < len(b) and b[s2].isdigit():
                s2 += 1
        else:
            numeric = False
            while s1 < len(a) and a[s1].isalpha():
                s1 += 1
            while s2 < len(b) and b[s2].isalpha():
                s2 += 1
        if s2 == two:
            return 1 if numeric else -1
        seg1, seg2 = a[one:s1], b[two:s2]
        if numeric:
            seg1, seg2 = seg1.lstrip("0"), seg2.lstrip("0")
            if len(seg1) != len(seg2):
                return 1 if len(seg1) > len(seg2) else -1
        if seg1 != seg2:
            return 1 if seg1 > seg2 else -1
        one, two = s1, s2
        prev1, prev2 = one, two
    if one >= len(a) and two >= len(b):
        return 0
    rest1 = a[one:]
    rest2 = b[two:]
    if (not rest1 and not rest2[:1].isalpha()) or rest1[:1].isalpha():
        return -1
    return 1


def _split(version):
    epoch = 0
    m = re.match(r"^(\d+):(.*)$", version)
    if m:
        epoch, version = int(m.group(1)), m.group(2)
    if "-" in version:
        ver, rel = version.rsplit("-", 1)
    else:
        ver, rel = version, ""
    return epoch, ver, rel


def vercmp(a, b):
    if a == b:
        return 0
    ea, va, ra = _split(a)
    eb, vb, rb = _split(b)
    if ea != eb:
        return 1 if ea > eb else -1
    c = _rpmvercmp(va, vb)
    if c:
        return c
    if ra and rb:
        return _rpmvercmp(ra, rb)
    return 0


def parse_info(text):
    info = {"depend": [], "conflict": [], "backup": []}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = [part.strip() for part in line.split("=", 1)]
        if key in ("depend", "conflict", "backup"):
            info[key].append(value)
        else:
            info[key] = value
    return info


def format_info(info):
    lines = []
    for key in ("pkgname", "pkgver", "pkgdesc", "arch", "size", "csize", "filename", "sha512"):
        if info.get(key) not in (None, ""):
            lines.append("%s = %s" % (key, info[key]))
    for key in ("depend", "conflict", "backup"):
        for value in info.get(key, []):
            lines.append("%s = %s" % (key, value))
    return "\n".join(lines) + "\n"


def make_tar_gz(entries):
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.GNU_FORMAT) as tar:
        for name, kind, mode, data in entries:
            info = tarfile.TarInfo(name)
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            info.mtime = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
            info.mode = mode
            if kind == "dir":
                info.type = tarfile.DIRTYPE
                tar.addfile(info)
            elif kind == "link":
                info.type = tarfile.SYMTYPE
                info.linkname = data
                tar.addfile(info)
            else:
                info.size = len(data)
                tar.addfile(info, io.BytesIO(data))
    out = io.BytesIO()
    with gzip.GzipFile(fileobj=out, mode="wb", mtime=0, compresslevel=9) as gz:
        gz.write(raw.getvalue())
    return out.getvalue()


def sha512_file(path):
    h = hashlib.sha512()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()
