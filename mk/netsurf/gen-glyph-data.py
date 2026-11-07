#!/usr/bin/env python3
import sys

GLYPH_ROWS = 16
RULE = "-----------------------------------------------------"
DOTS = "- - - - - - - - - - - - - - - - - - - - - - - - - - -"


def row_bits(value):
    return "".join("#" if value & (0x80 >> col) else "." for col in range(8))


def bold(rows):
    return [r | (r >> 1) for r in rows]


def italic(rows):
    out = []
    for index, r in enumerate(rows):
        if index < 5:
            out.append(r >> 1)
        elif index >= 11:
            out.append((r << 1) & 0xFF)
        else:
            out.append(r)
    return out


def read_unifont(path, first, last):
    glyphs = {}
    with open(path, "r", encoding="ascii") as handle:
        for line in handle:
            code, _, data = line.strip().partition(":")
            point = int(code, 16)
            if first <= point <= last and len(data) == 32:
                glyphs[point] = [int(data[i:i + 2], 16) for i in range(0, 32, 2)]
    return glyphs


def entry(point, rows):
    styles = [rows, italic(rows), bold(rows), italic(bold(rows))]
    lines = ["U+%04X - CYRILLIC BLOCK (GNU Unifont)" % point, DOTS]
    for index in range(GLYPH_ROWS):
        lines.append("   " + "   ".join(row_bits(style[index]) for style in styles))
    lines.append(RULE)
    return "\n".join(lines) + "\n"


def main():
    original, unifont, output = sys.argv[1], sys.argv[2], sys.argv[3]
    with open(original, "r", encoding="utf-8", errors="surrogateescape") as handle:
        text = handle.read()
    if not text.endswith("\n"):
        text += "\n"
    glyphs = read_unifont(unifont, 0x0400, 0x04FF)
    chunks = [text]
    for point in sorted(glyphs):
        chunks.append(entry(point, glyphs[point]))
    with open(output, "w", encoding="utf-8", errors="surrogateescape") as handle:
        handle.write("".join(chunks))
    print("glyph_data: +%d cyrillic glyphs -> %s" % (len(glyphs), output))


main()
