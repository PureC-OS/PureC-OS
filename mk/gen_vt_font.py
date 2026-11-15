import gzip
import struct
import sys

src = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/kbd/consolefonts/default8x16.psfu.gz"
dst = sys.argv[2] if len(sys.argv) > 2 else "src/drivers/display/vt_font.c"

data = gzip.open(src).read() if src.endswith(".gz") else open(src, "rb").read()
magic, version, hsize, flags, count, gsize, height, width = struct.unpack("<8I", data[:32])
if magic != 0x864AB572 or width != 8 or height != 16:
    raise SystemExit("need PSF2 8x16 font")
glyphs = [data[hsize + i * gsize: hsize + (i + 1) * gsize] for i in range(count)]
table = {}
if flags & 1:
    pos = hsize + count * gsize
    for index in range(count):
        entry = bytearray()
        while data[pos] != 0xFF:
            entry.append(data[pos])
            pos += 1
        pos += 1
        for cp in bytes(entry).split(b"\xfe")[0].decode("utf-8", "ignore"):
            table.setdefault(ord(cp), index)

out = list(glyphs[:256])
for cp in range(0x20, 0x7F):
    if cp in table:
        out[cp] = glyphs[table[cp]]

with open(dst, "w") as f:
    f.write("#include <stdint.h>\n\n")
    f.write("const uint8_t vt_font_8x16[256][16] = {\n")
    for g in out:
        f.write("    {" + ",".join("0x%02X" % b for b in g) + "},\n")
    f.write("};\n")
