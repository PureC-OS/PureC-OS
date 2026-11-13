#!/usr/bin/env python3
import sys
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont

NOTO = "/usr/share/fonts/noto/NotoSans-%s.ttf"
MONO = "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-%s.ttf"
TEXT_EXTRA = list(range(0x0400, 0x0460)) + [0x2014, 0x2013, 0x2026, 0x00AB, 0x00BB, 0x2022, 0x00B0,
                                             0x2116, 0x00B7, 0x2190, 0x2191, 0x2192, 0x2193, 0x00A9]
MONO_EXTRA = TEXT_EXTRA + list(range(0x2500, 0x2580)) + list(range(0x2580, 0x25A0)) + [0x25B2, 0x25BC, 0x25C6, 0x25CF]
NERD = "/usr/share/fonts/TTF/JetBrainsMonoNerdFontPropo-Regular.ttf"
ASCII = "".join(chr(c) for c in range(32, 127))

FONTS = [
    ("PGF_SMALL", "Regular", 12, ASCII),
    ("PGF_BODY", "Regular", 14, ASCII),
    ("PGF_MEDIUM", "Medium", 14, ASCII),
    ("PGF_HEAD", "Bold", 16, ASCII),
    ("PGF_TITLE", "Bold", 24, ASCII),
    ("PGF_HERO", "Bold", 34, ASCII),
    ("PGF_DIGITS", "Medium", 60, "0123456789: "),
    ("PGF_MONO_S", "mono:Regular", 14, ASCII),
    ("PGF_MONO_S_BOLD", "mono:Bold", 14, ASCII),
    ("PGF_MONO", "mono:Regular", 16, ASCII),
    ("PGF_MONO_BOLD", "mono:Bold", 16, ASCII),
    ("PGF_MONO_L", "mono:Regular", 19, ASCII),
    ("PGF_MONO_L_BOLD", "mono:Bold", 19, ASCII),
]

ICONS = [
    "wifi", "wifi_off", "wifi_strength_1", "wifi_strength_2", "wifi_strength_3",
    "wifi_strength_4", "wifi_strength_outline", "volume_high", "volume_medium",
    "volume_low", "volume_off", "monitor", "palette", "harddisk",
    "information_outline", "clock_outline", "power", "restart", "battery",
    "battery_charging", "battery_outline", "account_circle", "cog", "lock",
    "check", "check_circle", "chevron_right", "chevron_left", "ethernet",
    "memory", "cpu_64_bit", "speaker", "refresh", "wallpaper", "format_font",
    "theme_light_dark", "earth", "calendar", "timer_outline", "lan", "close",
    "alert_circle", "magnify", "usb", "server", "web", "brightness_6",
    "monitor_screenshot", "texture_box", "play_circle_outline", "music_note",
    "lock_open_variant", "access_point_network", "shield_check", "flash",
    "thermometer", "fan", "chip", "account", "keyboard", "folder", "download",
    "upload",
]
ICON_SIZES = [18, 28, 56]


def render_font(name, style, size, chars):
    mono = style.startswith("mono:")
    path = MONO % style[5:] if mono else NOTO % style
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()
    data = bytearray()
    cell_adv = int(round(font.getlength("M"))) if mono else 0

    def glyph(ch):
        adv = cell_adv if mono else int(round(font.getlength(ch)))
        l, t, r, b = font.getbbox(ch, anchor="ls")
        w, h = max(0, r - l), max(0, b - t)
        off = len(data)
        if w and h:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-l, -t), ch, font=font, fill=255, anchor="ls")
            data.extend(img.tobytes())
        return (off, w, h, max(-128, min(127, l)), max(-128, min(127, t + ascent)), adv)

    glyphs = [glyph(ch) if ch in chars else (0, 0, 0, 0, 0, 0) for ch in ASCII]
    extra = []
    if chars == ASCII:
        cmap = TTFont(path).getBestCmap()
        for cp in (MONO_EXTRA if mono else TEXT_EXTRA):
            if cp in cmap:
                extra.append((cp, glyph(chr(cp))))
    return dict(name=name, ascent=ascent, height=ascent + descent, glyphs=glyphs, data=data, extra=extra)


def render_icons():
    tt = TTFont(NERD)
    cmap = {}
    for cp, gname in tt.getBestCmap().items():
        cmap.setdefault(gname, cp)
    out = []
    for size in ICON_SIZES:
        font = ImageFont.truetype(NERD, size)
        for icon in ICONS:
            cp = cmap.get("md-" + icon)
            if cp is None:
                sys.exit("missing icon md-%s" % icon)
            ch = chr(cp)
            big = Image.new("L", (size * 3, size * 3), 0)
            ImageDraw.Draw(big).text((size, size), ch, font=font, fill=255)
            bbox = big.getbbox()
            if not bbox:
                sys.exit("empty icon " + icon)
            crop = big.crop(bbox)
            if crop.width > size or crop.height > size:
                crop.thumbnail((size, size), Image.LANCZOS)
            box = Image.new("L", (size, size), 0)
            box.paste(crop, ((size - crop.width) // 2, (size - crop.height) // 2))
            out.append((icon, size, box.tobytes()))
    return out


def c_bytes(data, indent="    "):
    lines = []
    for i in range(0, len(data), 24):
        lines.append(indent + ",".join(str(b) for b in data[i:i + 24]) + ",")
    return "\n".join(lines)


def main(path):
    fonts = [render_font(*f) for f in FONTS]
    icons = render_icons()
    h = ["#pragma once", "#include <stdint.h>", "",
         "struct pgf_glyph { uint32_t off; uint8_t w, h; int8_t x, y; uint8_t adv; };",
         "struct pgf_font { uint8_t ascent, height; const struct pgf_glyph *glyphs; const uint8_t *bits; uint16_t ext_count; const uint32_t *ext_cp; const struct pgf_glyph *ext; };", "",
         "enum pgf_font_id {"]
    for f in fonts:
        h.append("    %s," % f["name"])
    h += ["    PGF_COUNT", "};", "extern const struct pgf_font pgf_fonts[PGF_COUNT];", "", "enum pgf_icon_id {"]
    for icon in ICONS:
        h.append("    PGI_%s," % icon.upper())
    h += ["    PGI_COUNT", "};", "#define PGF_ICON_SIZES %d" % len(ICON_SIZES),
          "extern const uint8_t pgf_icon_size[PGF_ICON_SIZES];",
          "extern const uint8_t pgf_icon_bits[];",
          "extern const uint32_t pgf_icon_off[PGF_ICON_SIZES][PGI_COUNT];"]
    c = ['#include "include/pg_font_data.h"', ""]
    for f in fonts:
        n = f["name"].lower()
        c.append("static const uint8_t %s_bits[] = {" % n)
        c.append(c_bytes(f["data"]) if f["data"] else "    0,")
        c.append("};")
        c.append("static const struct pgf_glyph %s_glyphs[95] = {" % n)
        for gl in f["glyphs"]:
            c.append("    {%d,%d,%d,%d,%d,%d}," % gl)
        c.append("};")
        if f["extra"]:
            c.append("static const uint32_t %s_ext_cp[%d] = {%s};" % (n, len(f["extra"]), ",".join(str(cp) for cp, _ in f["extra"])))
            c.append("static const struct pgf_glyph %s_ext[%d] = {" % (n, len(f["extra"])))
            for _, gl in f["extra"]:
                c.append("    {%d,%d,%d,%d,%d,%d}," % gl)
            c.append("};")
    c.append("const struct pgf_font pgf_fonts[PGF_COUNT] = {")
    for f in fonts:
        n = f["name"].lower()
        if f["extra"]:
            c.append("    {%d,%d,%s_glyphs,%s_bits,%d,%s_ext_cp,%s_ext}," % (f["ascent"], f["height"], n, n, len(f["extra"]), n, n))
        else:
            c.append("    {%d,%d,%s_glyphs,%s_bits,0,0,0}," % (f["ascent"], f["height"], n, n))
    c.append("};")
    c.append("const uint8_t pgf_icon_size[PGF_ICON_SIZES] = {%s};" % ",".join(map(str, ICON_SIZES)))
    blob = bytearray()
    offs = []
    for icon, size, bits in icons:
        offs.append(len(blob))
        blob += bits
    c.append("const uint8_t pgf_icon_bits[] = {")
    c.append(c_bytes(blob))
    c.append("};")
    c.append("const uint32_t pgf_icon_off[PGF_ICON_SIZES][PGI_COUNT] = {")
    for si in range(len(ICON_SIZES)):
        c.append("    {" + ",".join(str(offs[si * len(ICONS) + i]) for i in range(len(ICONS))) + "},")
    c.append("};")
    with open(path, "w") as fh:
        fh.write("\n".join(h) + "\n")
    cpath = path.replace("include/pg_font_data.h", "font_data.c")
    with open(cpath, "w") as fh:
        fh.write("\n".join(c) + "\n")
    print("wrote %s and %s" % (path, cpath))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "src/libgui/include/pg_font_data.h")
