#include "hexedit/input/input.hpp"
#include "hexedit/buffer/buffer.hpp"
#include "hexedit/tree/tree.hpp"
#include "hexedit/view/view.hpp"
#include "hexedit/window/window.hpp"

extern "C" {
#include "../../../../libc/include/purec.h"
}

void hex_handle_hex(char c)
{
    int v = hex_val(c);
    if (v < 0)
    {
        return;
    }

    if (hex_cursor == hex_buf_len)
    {
        if (!hex_insert_byte(hex_cursor, uint8_t(v << 4)))
        {
            hex_set_status("No memory", true);
            return;
        }
        hex_high_nibble = false;
        hex_dirty = true;
        hex_set_status("", false);
        return;
    }

    if (hex_high_nibble)
    {
        hex_buf[hex_cursor] = uint8_t((v << 4) | (hex_buf[hex_cursor] & 0x0F));
        hex_high_nibble = false;
    }
    else
    {
        hex_buf[hex_cursor] = uint8_t((hex_buf[hex_cursor] & 0xF0) | v);
        hex_cursor++;
        if (hex_cursor > hex_buf_len)
        {
            hex_cursor = hex_buf_len;
        }
        hex_high_nibble = true;
    }

    hex_dirty = true;
    hex_ensure_visible();
    hex_set_status("", false);
}

void hex_handle_ascii(char c)
{
    if (c < ' ' || c > '~')
    {
        return;
    }

    if (hex_cursor == hex_buf_len)
    {
        if (!hex_insert_byte(hex_cursor, uint8_t(c)))
        {
            hex_set_status("No memory", true);
            return;
        }
        hex_cursor++;
        hex_dirty = true;
        hex_ensure_visible();
        return;
    }

    if (hex_insert_mode)
    {
        if (!hex_insert_byte(hex_cursor, uint8_t(c)))
        {
            hex_set_status("No memory", true);
            return;
        }
        hex_cursor++;
    }
    else
    {
        hex_buf[hex_cursor++] = uint8_t(c);
        if (hex_cursor > hex_buf_len)
        {
            hex_buf_len = hex_cursor;
        }
    }

    hex_dirty = true;
    hex_ensure_visible();
    hex_set_status("", false);
}

bool hex_handle_prompt_key(int32_t k)
{
    if (k == 27)
    {
        hex_exit_prompt(false);
        return true;
    }
    if (k == '\r' || k == '\n')
    {
        hex_exit_prompt(true);
        return true;
    }

    uint32_t l = pc_strlen(hex_prompt_buf);

    if (k == '\b' || k == 127)
    {
        if (l)
        {
            hex_prompt_buf[l - 1] = '\0';
        }
        return true;
    }

    if (k >= ' ' && k <= '~' && l + 1 < sizeof(hex_prompt_buf))
    {
        hex_prompt_buf[l] = char(k);
        hex_prompt_buf[l + 1] = '\0';
        return true;
    }

    return true;
}

bool hex_handle_special(uint8_t k)
{
    if (hex_prompt != HEXEDIT_PROMPT_NONE)
    {
        return false;
    }

    uint32_t rows = hex_visible_rows();

    switch (k)
    {
        case 4:
            hex_cursor = (hex_cursor / HEXEDIT_BYTES_PER_ROW) * HEXEDIT_BYTES_PER_ROW;
            hex_high_nibble = true;
            hex_ensure_visible();
            return true;

        case 5:
        {
            uint32_t rs = (hex_cursor / HEXEDIT_BYTES_PER_ROW) * HEXEDIT_BYTES_PER_ROW;
            uint32_t re = rs + HEXEDIT_BYTES_PER_ROW;
            if (re > hex_buf_len)
            {
                re = hex_buf_len;
            }
            hex_cursor = re;
            if (hex_cursor > rs && hex_cursor == re && re != hex_buf_len)
            {
                hex_cursor = re - 1;
            }
            hex_high_nibble = true;
            hex_ensure_visible();
            return true;
        }

        case 6:
            hex_move_cursor(-int32_t(rows * HEXEDIT_BYTES_PER_ROW));
            return true;

        case 7:
            hex_move_cursor(int32_t(rows * HEXEDIT_BYTES_PER_ROW));
            return true;

        case 8:
            if (hex_mode && !hex_high_nibble)
            {
                hex_high_nibble = true;
            }
            else if (hex_cursor > 0)
            {
                hex_cursor--;
                hex_high_nibble = true;
            }
            hex_ensure_visible();
            return true;

        case 9:
            if (hex_mode && hex_high_nibble && hex_cursor < hex_buf_len)
            {
                hex_high_nibble = false;
            }
            else if (hex_cursor < hex_buf_len)
            {
                hex_cursor++;
                hex_high_nibble = true;
            }
            else if (hex_cursor == hex_buf_len && hex_high_nibble)
            {
                hex_high_nibble = false;
            }
            hex_ensure_visible();
            return true;

        case 10:
            hex_move_cursor(-int32_t(HEXEDIT_BYTES_PER_ROW));
            return true;

        case 11:
            hex_move_cursor(int32_t(HEXEDIT_BYTES_PER_ROW));
            return true;

        case 12:
            if (hex_cursor < hex_buf_len)
            {
                hex_delete_byte(hex_cursor);
                hex_dirty = true;
                hex_set_status("", false);
            }
            return true;

        case 1:
            hex_insert_mode = !hex_insert_mode;
            hex_set_status(hex_insert_mode ? "Insert" : "Overwrite", false);
            return true;

        default:
            return false;
    }
}

bool hex_handle_mouse(const pg_event* ev)
{
    if (!ev)
    {
        return false;
    }
    if (ev->type != PG_EVENT_MOUSE_UP || ev->button != 1)
    {
        return false;
    }

    auto c = hex_window.client();
    int32_t mx = ev->x - int32_t(c.x);
    int32_t my = ev->y - int32_t(c.y);

    if (mx >= 0 && mx < int32_t(HEXEDIT_SIDEBAR_W))
    {
        uint32_t top = HEXEDIT_TOOLBAR_H;

        if (my >= int32_t(top + 24) && my < int32_t(top + 42))
        {
            char p[128];
            hex_parent_dir(p, sizeof(p), hex_dir_path);
            hex_normalize_dir(hex_dir_path, sizeof(hex_dir_path), p);
            hex_refresh_dir();
            return true;
        }

        uint32_t list_top = top + 46;

        if (my >= int32_t(list_top))
        {
            uint32_t row = (uint32_t(my - int32_t(list_top))) / 18;
            uint32_t idx = hex_tree_scroll + row;

            if (int32_t(idx) < hex_entry_count)
            {
                if (hex_entries[idx].is_dir)
                {
                    char np[128];
                    if (hex_join_path(np, sizeof(np), hex_dir_path, hex_entries[idx].name))
                    {
                        hex_normalize_dir(hex_dir_path, sizeof(hex_dir_path), np);
                        hex_refresh_dir();
                    }
                }
                else
                {
                    char fp[128];
                    if (hex_join_path(fp, sizeof(fp), hex_dir_path, hex_entries[idx].name))
                    {
                        if (hex_dirty)
                        {
                            hex_set_status("Unsaved changes — Ctrl+S to save", true);
                        }
                        hex_sel_entry = int32_t(idx);
                        (void)hex_load_file(fp);
                    }
                }
                return true;
            }
        }

        return false;
    }

    uint32_t top = HEXEDIT_TOOLBAR_H + HEXEDIT_HEADER_H;
    if (my < int32_t(top) || my >= int32_t(c.height - HEXEDIT_STATUS_H))
    {
        return false;
    }

    uint32_t row = (uint32_t(my - int32_t(top))) / HEXEDIT_ROW_H;
    uint32_t off = hex_view_offset + row * HEXEDIT_BYTES_PER_ROW;
    uint32_t hex_x = HEXEDIT_SIDEBAR_W + 8 + HEXEDIT_OFFSET_CHARS * 8 + 8;
    uint32_t ascii_x = hex_x + HEXEDIT_BYTES_PER_ROW * 3 * 8 + 4 + 16;

    if (mx >= int32_t(hex_x) && mx < int32_t(hex_x + HEXEDIT_BYTES_PER_ROW * 3 * 8 + 4))
    {
        int32_t rel = mx - int32_t(hex_x);
        uint32_t col;

        if (rel < int32_t(8 * 3 * 8))
        {
            col = uint32_t(rel) / (3 * 8);
        }
        else if (rel < int32_t(8 * 3 * 8 + 4))
        {
            return false;
        }
        else
        {
            col = 8 + uint32_t(rel - (8 * 3 * 8 + 4)) / (3 * 8);
        }

        if (col >= HEXEDIT_BYTES_PER_ROW)
        {
            return false;
        }

        uint32_t tgt = off + col;
        if (tgt > hex_buf_len)
        {
            tgt = hex_buf_len;
        }

        hex_cursor = tgt;

        uint32_t cx = hex_x + col * 3 * 8 + (col >= 8 ? 4 : 0);
        int32_t within = mx - int32_t(cx);
        hex_high_nibble = within < 8;
        hex_mode = true;
        hex_ensure_visible();
        return true;
    }
    else if (mx >= int32_t(ascii_x) && mx < int32_t(ascii_x + HEXEDIT_BYTES_PER_ROW * 8))
    {
        uint32_t col = uint32_t(mx - int32_t(ascii_x)) / 8;
        if (col >= HEXEDIT_BYTES_PER_ROW)
        {
            return false;
        }

        uint32_t tgt = off + col;
        if (tgt > hex_buf_len)
        {
            tgt = hex_buf_len;
        }

        hex_cursor = tgt;
        hex_mode = false;
        hex_high_nibble = true;
        hex_ensure_visible();
        return true;
    }

    return false;
}
