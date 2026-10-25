#include "hexedit/view/view.hpp"
#include "hexedit/buffer/buffer.hpp"
#include "hexedit/tree/tree.hpp"
#include "hexedit/window/window.hpp"

extern "C" {
#include "../../../../libc/include/purec.h"
}

void hex_enter_prompt(HexPromptMode m)
{
    hex_prompt = m;
    hex_prompt_buf[0] = '\0';
    hex_set_status("", false);
}

static void hex_do_search()
{
    if (!hex_prompt_buf[0])
    {
        hex_set_status("Empty search", true);
        return;
    }

    uint32_t plen = pc_strlen(hex_prompt_buf);
    uint32_t start = hex_cursor + 1;

    if (start > hex_buf_len)
    {
        start = 0;
    }

    for (uint32_t i = start; i + plen <= hex_buf_len; ++i)
    {
        bool mm = true;
        for (uint32_t j = 0; j < plen; ++j)
        {
            if (hex_buf[i + j] != uint8_t(hex_prompt_buf[j]))
            {
                mm = false;
                break;
            }
        }
        if (mm)
        {
            hex_cursor = i;
            hex_high_nibble = true;
            hex_ensure_visible();
            hex_set_status("Found", false);
            return;
        }
    }

    for (uint32_t i = 0; i + plen <= hex_buf_len && i < start; ++i)
    {
        bool mm = true;
        for (uint32_t j = 0; j < plen; ++j)
        {
            if (hex_buf[i + j] != uint8_t(hex_prompt_buf[j]))
            {
                mm = false;
                break;
            }
        }
        if (mm)
        {
            hex_cursor = i;
            hex_high_nibble = true;
            hex_ensure_visible();
            hex_set_status("Found (wrapped)", false);
            return;
        }
    }

    hex_set_status("Not found", true);
}

void hex_exit_prompt(bool ok)
{
    if (!ok)
    {
        hex_prompt = HEXEDIT_PROMPT_NONE;
        hex_set_status("Cancelled", false);
        return;
    }

    if (hex_prompt == HEXEDIT_PROMPT_GOTO)
    {
        bool good;
        uint32_t off = hex_parse_hex(hex_prompt_buf, &good);
        if (!good)
        {
            hex_set_status("Invalid hex", true);
            hex_prompt = HEXEDIT_PROMPT_NONE;
            return;
        }
        if (off > hex_buf_len)
        {
            off = hex_buf_len;
        }
        hex_cursor = off;
        hex_high_nibble = true;
        hex_ensure_visible();
        hex_set_status("Goto done", false);
    }
    else if (hex_prompt == HEXEDIT_PROMPT_SEARCH)
    {
        hex_do_search();
    }

    hex_prompt = HEXEDIT_PROMPT_NONE;
}

static void view_toolbar()
{
    auto* w = &hex_window.gui;
    auto c = hex_window.client();

    pg_window_rect(w, {0, 0, c.width, HEXEDIT_TOOLBAR_H}, 0x252638);
    pg_window_text(w, 8, 8, hex_file_path[0] ? hex_file_path : "(no file)", w->theme.text);

    char info[64] = "";
    char dec[12];
    hex_u32_dec(dec, hex_buf_len);
    pc_copy(info, dec, sizeof(info));
    hex_append_txt(info, " bytes", sizeof(info));
    if (hex_dirty)
    {
        hex_append_txt(info, " *", sizeof(info));
    }
    pg_window_text(w, c.width - pc_strlen(info) * 8 - 8, 8, info, hex_dirty ? 0xF9E2AF : w->theme.muted_text);

    const char* mode = hex_mode ? "HEX" : "ASCII";
    const char* im = hex_insert_mode ? "INS" : "OVR";
    char ms[20] = "";
    pc_copy(ms, mode, sizeof(ms));
    hex_append_txt(ms, "/", sizeof(ms));
    hex_append_txt(ms, im, sizeof(ms));
    pg_window_text(w, c.width / 2 - 20, 8, ms, hex_mode ? 0x89B4FA : 0xA6E3A1);
}

static void view_header(uint32_t hex_x)
{
    auto* w = &hex_window.gui;
    auto c = hex_window.client();
    uint32_t y = HEXEDIT_TOOLBAR_H;

    pg_window_rect(w, {HEXEDIT_SIDEBAR_W, y, c.width - HEXEDIT_SIDEBAR_W, HEXEDIT_HEADER_H}, 0x1E1E2E);
    pg_window_text(w, HEXEDIT_SIDEBAR_W + 8, y + 4, "Offset", 0x7F849C);

    for (uint32_t i = 0; i < HEXEDIT_BYTES_PER_ROW; ++i)
    {
        char h[3];
        h[0] = hex_digit(i >> 4);
        h[1] = hex_digit(i);
        h[2] = '\0';
        uint32_t x = hex_x + i * 3 * 8 + (i >= 8 ? 4 : 0);
        pg_window_text(w, x, y + 4, h, 0x7F849C);
    }
}

static void view_status()
{
    auto* w = &hex_window.gui;
    auto c = hex_window.client();
    uint32_t y = c.height - HEXEDIT_STATUS_H;

    pg_window_rect(w, {0, y, c.width, HEXEDIT_STATUS_H}, 0x292A3D);

    if (hex_prompt != HEXEDIT_PROMPT_NONE)
    {
        const char* lab = hex_prompt == HEXEDIT_PROMPT_GOTO ? "Goto (hex): " : "Search (ascii): ";
        pg_window_text(w, 8, y + 8, lab, 0xCDD6F4);
        uint32_t lx = pc_strlen(lab) * 8 + 12;
        pg_window_text(w, lx, y + 8, hex_prompt_buf, 0xA6E3A1);
        uint32_t cx = lx + pc_strlen(hex_prompt_buf) * 8;
        pg_window_rect(w, {cx, y + 6, 8, 14}, 0x89B4FA);
        pg_window_text(w, c.width - 160, y + 8, "Enter OK Esc cancel", 0x7F849C);
        return;
    }

    char left[96] = "";

    if (hex_status_msg[0])
    {
        pc_copy(left, hex_status_msg, sizeof(left));
        pg_window_text(w, 8, y + 8, left, hex_status_err ? 0xF38BA8 : 0xF9E2AF);
    }
    else
    {
        char off[9];
        hex_u32_hex(off, hex_cursor, 8);
        pc_copy(left, off, sizeof(left));
        hex_append_txt(left, "  ", sizeof(left));
        char dec[12];
        hex_u32_dec(dec, hex_cursor);
        hex_append_txt(left, dec, sizeof(left));
        if (hex_cursor < hex_buf_len)
        {
            hex_append_txt(left, "  val 0x", sizeof(left));
            char hb[3];
            hb[0] = hex_digit(hex_buf[hex_cursor] >> 4);
            hb[1] = hex_digit(hex_buf[hex_cursor]);
            hb[2] = '\0';
            hex_append_txt(left, hb, sizeof(left));
            char a[8];
            a[0] = ' ';
            a[1] = '\'';
            a[2] = (hex_buf[hex_cursor] >= 32 && hex_buf[hex_cursor] <= 126) ? char(hex_buf[hex_cursor]) : '.';
            a[3] = '\'';
            a[4] = '\0';
            hex_append_txt(left, a, sizeof(left));
        }
        pg_window_text(w, 8, y + 8, left, w->theme.muted_text);
    }

    const char* help = "Ctrl+S save Ctrl+X exit Ctrl+G goto Ctrl+F find Tab HEX/ASCII F1 OVR/INS";
    uint32_t hw = pc_strlen(help) * 8;
    if (!hex_status_msg[0] && hw + 160 < c.width)
    {
        pg_window_text(w, c.width - hw - 8, y + 8, help, 0x6C7086);
    }
}

static void view_sidebar()
{
    auto* w = &hex_window.gui;
    auto c = hex_window.client();
    uint32_t top = HEXEDIT_TOOLBAR_H;
    uint32_t h = c.height - HEXEDIT_TOOLBAR_H - HEXEDIT_STATUS_H;

    pg_window_rect(w, {0, top, HEXEDIT_SIDEBAR_W, h}, 0x202131);
    pg_window_text(w, 8, top + 6, hex_dir_path, 0xCDD6F4);
    pg_window_rect(w, {8, top + 20, HEXEDIT_SIDEBAR_W - 16, 1}, 0x313244);
    pg_window_rect(w, {8, top + 24, HEXEDIT_SIDEBAR_W - 16, 18}, 0x313244);
    pg_window_text(w, 12, top + 28, ".. (up)", 0x89B4FA);

    uint32_t list_top = top + 46;
    uint32_t list_h = h - 46;

    hex_tree_visible_rows = list_h / 18;
    if (hex_tree_visible_rows == 0)
    {
        hex_tree_visible_rows = 1;
    }
    if (hex_tree_scroll + hex_tree_visible_rows > uint32_t(hex_entry_count) && hex_entry_count > 0)
    {
        hex_tree_scroll = hex_entry_count > int32_t(hex_tree_visible_rows) ? hex_entry_count - hex_tree_visible_rows : 0;
    }

    for (uint32_t i = 0; i < hex_tree_visible_rows; ++i)
    {
        uint32_t idx = hex_tree_scroll + i;
        if (int32_t(idx) >= hex_entry_count)
        {
            break;
        }

        uint32_t y = list_top + i * 18;
        bool sel = int32_t(idx) == hex_sel_entry;

        pg_window_rect(w, {6, y, HEXEDIT_SIDEBAR_W - 12, 16}, sel ? 0x45475A : (i % 2 ? 0x252638 : 0x202131));

        if (hex_entries[idx].is_dir)
        {
            pg_window_rect(w, {10, y + 3, 12, 8}, 0xF9E2AF);
            pg_window_rect(w, {10, y + 8, 14, 6}, 0xF9E2AF);
        }
        else
        {
            pg_window_rect(w, {10, y + 2, 10, 12}, 0xCDD6F4);
        }

        pg_window_text(w, 28, y + 4, hex_entries[idx].name, sel ? 0xCDD6F4 : (hex_entries[idx].is_dir ? 0x89B4FA : w->theme.text));

        if (!hex_entries[idx].is_dir)
        {
            char sz[16] = "";
            uint64_t s = hex_entries[idx].size;

            if (s >= 1024)
            {
                char d[12];
                hex_u32_dec(d, uint32_t(s / 1024));
                pc_copy(sz, d, sizeof(sz));
                hex_append_txt(sz, "K", sizeof(sz));
            }
            else
            {
                char d[12];
                hex_u32_dec(d, uint32_t(s));
                pc_copy(sz, d, sizeof(sz));
            }

            pg_window_text(w, HEXEDIT_SIDEBAR_W - 40, y + 4, sz, 0x9399B2);
        }
    }

    if (uint32_t(hex_entry_count) > hex_tree_visible_rows)
    {
        uint32_t bar_h = (hex_tree_visible_rows * list_h) / uint32_t(hex_entry_count);
        if (bar_h < 12)
        {
            bar_h = 12;
        }
        uint32_t bar_y = list_top + (hex_tree_scroll * (list_h - bar_h)) / (hex_entry_count - hex_tree_visible_rows);
        pg_window_rect(w, {HEXEDIT_SIDEBAR_W - 4, bar_y, 2, bar_h}, 0x585B70);
    }
}

static void view_hex(uint32_t hex_x, uint32_t ascii_x)
{
    auto* w = &hex_window.gui;
    auto c = hex_window.client();
    uint32_t top = HEXEDIT_TOOLBAR_H + HEXEDIT_HEADER_H;
    uint32_t rows = hex_visible_rows();
    uint32_t right_w = c.width - HEXEDIT_SIDEBAR_W;

    pg_window_rect(w, {HEXEDIT_SIDEBAR_W, top, right_w, rows * HEXEDIT_ROW_H}, 0x1E1E2E);

    for (uint32_t r = 0; r < rows; ++r)
    {
        uint32_t off = hex_view_offset + r * HEXEDIT_BYTES_PER_ROW;
        uint32_t y = top + r * HEXEDIT_ROW_H;

        if (r % 2 == 1)
        {
            pg_window_rect(w, {HEXEDIT_SIDEBAR_W, y, right_w, HEXEDIT_ROW_H}, 0x202131);
        }

        char offs[9];
        hex_u32_hex(offs, off, 8);
        bool row_cur = hex_cursor >= off && hex_cursor < off + HEXEDIT_BYTES_PER_ROW;
        pg_window_text(w, HEXEDIT_SIDEBAR_W + 8, y + 4, offs, row_cur ? 0x89B4FA : 0x7F849C);

        for (uint32_t col = 0; col < HEXEDIT_BYTES_PER_ROW; ++col)
        {
            uint32_t abs = off + col;
            uint32_t x = hex_x + col * 3 * 8 + (col >= 8 ? 4 : 0);

            if (abs < hex_buf_len)
            {
                char hb[3];
                hb[0] = hex_digit(hex_buf[abs] >> 4);
                hb[1] = hex_digit(hex_buf[abs]);
                hb[2] = '\0';
                bool cur = abs == hex_cursor;

                if (cur)
                {
                    uint32_t bg = hex_mode ? 0x89B4FA : 0x45475A;
                    uint32_t fg = hex_mode ? 0x1E1E2E : 0xCDD6F4;
                    pg_window_rect(w, {x, y + 2, 16, 12}, bg);
                    pg_window_text(w, x, y + 4, hb, fg);
                    if (hex_mode && !hex_high_nibble)
                    {
                        pg_window_rect(w, {x + 8, y + 12, 8, 2}, 0xF9E2AF);
                    }
                }
                else
                {
                    pg_window_text(w, x, y + 4, hb, w->theme.text);
                }
            }
            else if (abs == hex_buf_len && hex_cursor == hex_buf_len && col == (hex_buf_len % HEXEDIT_BYTES_PER_ROW))
            {
                pg_window_rect(w, {x, y + 2, 16, 12}, hex_mode ? 0x89B4FA : 0x45475A);
                pg_window_text(w, x, y + 4, "..", hex_mode ? 0x1E1E2E : 0xCDD6F4);
            }
        }

        pg_window_rect(w, {hex_x + HEXEDIT_BYTES_PER_ROW * 3 * 8 + 8, y, 1, HEXEDIT_ROW_H}, 0x313244);

        for (uint32_t col = 0; col < HEXEDIT_BYTES_PER_ROW; ++col)
        {
            uint32_t abs = off + col;
            uint32_t x = ascii_x + col * 8;

            if (abs < hex_buf_len)
            {
                char ch = char(hex_buf[abs]);
                char s[2];
                s[0] = (ch >= 32 && ch <= 126) ? ch : '.';
                s[1] = '\0';
                bool cur = abs == hex_cursor;

                if (cur && !hex_mode)
                {
                    pg_window_rect(w, {x, y + 2, 8, 12}, 0xA6E3A1);
                    pg_window_text(w, x, y + 4, s, 0x1E1E2E);
                }
                else if (cur && hex_mode)
                {
                    pg_window_rect(w, {x, y + 2, 8, 12}, 0x45475A);
                    pg_window_text(w, x, y + 4, s, w->theme.text);
                }
                else
                {
                    pg_window_text(w, x, y + 4, s, (hex_buf[abs] >= 32 && hex_buf[abs] <= 126) ? w->theme.text : w->theme.muted_text);
                }
            }
            else if (abs == hex_buf_len && hex_cursor == hex_buf_len && col == (hex_buf_len % HEXEDIT_BYTES_PER_ROW))
            {
                pg_window_rect(w, {x, y + 2, 8, 12}, !hex_mode ? 0xA6E3A1 : 0x45475A);
                pg_window_text(w, x, y + 4, " ", !hex_mode ? 0x1E1E2E : w->theme.text);
            }
        }
    }
}

bool hex_do_draw()
{
    if (hex_window.isMinimized())
    {
        return true;
    }

    hex_window.begin();

    if (hex_window.isMinimized())
    {
        hex_window.end();
        return true;
    }

    auto c = hex_window.client();
    pg_window_clear(&hex_window.gui, 0x1E1E2E);
    view_toolbar();
    uint32_t hex_x = HEXEDIT_SIDEBAR_W + 8 + HEXEDIT_OFFSET_CHARS * 8 + 8;
    uint32_t ascii_x = hex_x + HEXEDIT_BYTES_PER_ROW * 3 * 8 + 4 + 16;
    view_header(hex_x);
    view_sidebar();
    pg_window_rect(&hex_window.gui, {HEXEDIT_SIDEBAR_W, HEXEDIT_TOOLBAR_H, 1, c.height - HEXEDIT_TOOLBAR_H - HEXEDIT_STATUS_H}, 0x313244);
    view_hex(hex_x, ascii_x);
    view_status();
    hex_window.end();
    return true;
}
