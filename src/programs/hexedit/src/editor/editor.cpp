#include "hexedit/editor/editor.hpp"
#include "hexedit/buffer/buffer.hpp"
#include "hexedit/tree/tree.hpp"
#include "hexedit/window/window.hpp"
#include "hexedit/view/view.hpp"
#include "hexedit/input/input.hpp"

extern "C" {
#include "../../../../libc/include/purec.h"
}

int hexedit_run(const char* initial_path)
{
    if (initial_path && initial_path[0])
    {
        if (hex_is_dir_path(initial_path))
        {
            hex_normalize_dir(hex_dir_path, sizeof(hex_dir_path), initial_path);
            hex_file_path[0] = '\0';
            hex_buf_len = 0;
            hex_cursor = 0;
            hex_view_offset = 0;
            hex_dirty = false;
            if (!hex_reserve(4096))
            {
                return 1;
            }
            hex_refresh_dir();
        }
        else
        {
            if (!hex_reserve(4096))
            {
                return 1;
            }
            if (!hex_load_file(initial_path))
            {
                hex_normalize_dir(hex_dir_path, sizeof(hex_dir_path), "/");
                hex_refresh_dir();
                pc_copy(hex_file_path, initial_path, sizeof(hex_file_path));
                hex_buf_len = 0;
            }
        }
    }
    else
    {
        hex_normalize_dir(hex_dir_path, sizeof(hex_dir_path), "/");
        hex_file_path[0] = '\0';
        hex_buf_len = 0;
        if (!hex_reserve(4096))
        {
            return 1;
        }
        hex_refresh_dir();
        hex_set_status("Select a file from the left tree", false);
    }

    if (!hex_window.init())
    {
        return 1;
    }
    if (!hex_do_draw())
    {
        return 1;
    }

    bool exit_armed = false;

    for (;;)
    {
        pg_event ev;

        if (!hex_window.poll(&ev))
        {
            pc_sleep(16);
            continue;
        }

        if (ev.type == PG_EVENT_CLOSE)
        {
            hex_window.shutdown();
            return 0;
        }

        if (ev.type == PG_EVENT_MOVE || ev.type == PG_EVENT_MINIMIZE || ev.type == PG_EVENT_FOCUS || ev.type == PG_EVENT_REPAINT)
        {
            (void)hex_do_draw();
            continue;
        }

        if (ev.type == PG_EVENT_MOUSE_DOWN || ev.type == PG_EVENT_MOUSE_UP || ev.type == PG_EVENT_MOUSE_MOVE)
        {
            if (ev.type == PG_EVENT_MOUSE_UP && hex_handle_mouse(&ev))
            {
                (void)hex_do_draw();
            }
            continue;
        }

        if (ev.type == PG_EVENT_SPECIAL_KEY)
        {
            if (hex_handle_special(uint8_t(ev.key)))
            {
                exit_armed = false;
                (void)hex_do_draw();
            }
            continue;
        }

        if (ev.type != PG_EVENT_KEY || hex_window.isMinimized())
        {
            continue;
        }

        char ch = char(ev.key);

        if (hex_prompt != HEXEDIT_PROMPT_NONE)
        {
            hex_handle_prompt_key(ev.key);
            (void)hex_do_draw();
            continue;
        }

        if (ch == 19)
        {
            exit_armed = false;
            if (hex_save_file())
            {
                hex_set_status("Saved", false);
            }
            else
            {
                hex_set_status(!hex_file_path[0] ? "No file selected" : "Save failed", true);
            }
            (void)hex_do_draw();
            continue;
        }

        if (ch == 24)
        {
            if (hex_dirty && !exit_armed)
            {
                exit_armed = true;
                hex_set_status("Unsaved Ctrl+X again discards", true);
                (void)hex_do_draw();
                continue;
            }
            hex_window.shutdown();
            return 0;
        }

        if (ch == 7)
        {
            exit_armed = false;
            hex_enter_prompt(HEXEDIT_PROMPT_GOTO);
            (void)hex_do_draw();
            continue;
        }

        if (ch == 6)
        {
            exit_armed = false;
            hex_enter_prompt(HEXEDIT_PROMPT_SEARCH);
            (void)hex_do_draw();
            continue;
        }

        if (ch == '\t')
        {
            hex_mode = !hex_mode;
            hex_high_nibble = true;
            (void)hex_do_draw();
            continue;
        }

        if (ch == '\r')
        {
            ch = '\n';
        }

        if (ch == '\b' || ch == 127)
        {
            if (hex_cursor)
            {
                hex_cursor--;
                hex_delete_byte(hex_cursor);
                hex_dirty = true;
                hex_high_nibble = true;
                hex_ensure_visible();
                hex_set_status("", false);
                (void)hex_do_draw();
            }
            continue;
        }

        if (ch == '\n')
        {
            hex_move_cursor(HEXEDIT_BYTES_PER_ROW - (hex_cursor % HEXEDIT_BYTES_PER_ROW));
            (void)hex_do_draw();
            continue;
        }

        if (hex_file_path[0] == 0 && hex_buf_len == 0)
        {
            hex_set_status("Select or create a file first (click left tree)", true);
            (void)hex_do_draw();
            continue;
        }

        if (hex_mode)
        {
            if (hex_val(ch) >= 0)
            {
                hex_handle_hex(ch);
                (void)hex_do_draw();
            }
        }
        else
        {
            if (ch >= ' ' && ch <= '~')
            {
                hex_handle_ascii(ch);
                (void)hex_do_draw();
            }
        }
    }
}
