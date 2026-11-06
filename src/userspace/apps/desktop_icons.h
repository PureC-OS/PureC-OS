#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Draws the alpha-mask icon (.icn, see mk/gen_icons.py) named by a desktop
 * entry inside the box (x, y, box_w, box_h), tinted with a colour that
 * contrasts with tile_color. Returns false when the entry has no usable icon
 * so the caller can fall back to its text symbol.
 */
bool desktop_icon_draw(uint32_t index, const char *icon_path, uint32_t x,
                       uint32_t y, uint32_t box_w, uint32_t box_h,
                       uint32_t tile_color);
