#include "hexedit/buffer/buffer.hpp"
#include "hexedit/window/window.hpp"

extern "C" {
#include "../../../../libc/include/purec.h"
#include "../../../../libfs/include/purefs.h"
}

#include <stdint.h>

uint8_t* hex_buf = nullptr;
uint32_t hex_buf_len = 0;
uint32_t hex_buf_cap = 0;
uint32_t hex_cursor = 0;
bool hex_high_nibble = true;
bool hex_mode = true;
bool hex_insert_mode = false;
bool hex_dirty = false;
uint32_t hex_view_offset = 0;
char hex_file_path[128] = "";
char hex_status_msg[96] = "";
bool hex_status_err = false;
HexPromptMode hex_prompt = HEXEDIT_PROMPT_NONE;
char hex_prompt_buf[64] = "";

void hex_set_status(const char* m, bool err)
{
    pc_copy(hex_status_msg, m ? m : "", sizeof(hex_status_msg));
    hex_status_err = err;
}

char hex_digit(uint8_t v)
{
    v &= 0xF;
    if (v < 10)
    {
        return char('0' + v);
    }
    return char('A' + v - 10);
}

int hex_val(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

void hex_u32_hex(char* out, uint32_t v, uint32_t d)
{
    for (int i = int(d) - 1; i >= 0; --i)
    {
        out[i] = hex_digit(v & 0xF);
        v >>= 4;
    }
    out[d] = '\0';
}

void hex_u32_dec(char* out, uint32_t v)
{
    char rev[11];
    uint32_t n = 0;

    if (v == 0)
    {
        rev[n++] = '0';
    }
    else
    {
        while (v)
        {
            rev[n++] = char('0' + v % 10);
            v /= 10;
        }
    }

    uint32_t i = 0;
    while (n)
    {
        out[i++] = rev[--n];
    }
    out[i] = '\0';
}

void hex_append_txt(char* dst, const char* src, uint32_t cap)
{
    uint32_t pos = pc_strlen(dst);
    if (pos >= cap)
    {
        return;
    }
    pc_copy(dst + pos, src, cap - pos);
}

bool hex_reserve(uint32_t need)
{
    if (need <= hex_buf_cap)
    {
        return true;
    }

    uint32_t cap = hex_buf_cap ? hex_buf_cap : 4096;

    while (cap < need)
    {
        if (cap > UINT32_MAX / 2)
        {
            cap = need;
            break;
        }
        cap *= 2;
    }

    void* p = pc_heap_grow(cap - hex_buf_cap);
    if (!p)
    {
        return false;
    }
    if (hex_buf && p != (void*)(hex_buf + hex_buf_cap))
    {
        return false;
    }
    if (!hex_buf)
    {
        hex_buf = (uint8_t*)p;
    }
    hex_buf_cap = cap;
    return true;
}

uint32_t hex_visible_rows()
{
    auto c = hex_window.client();
    if (c.height < HEXEDIT_TOOLBAR_H + HEXEDIT_HEADER_H + HEXEDIT_STATUS_H + HEXEDIT_ROW_H)
    {
        return 1;
    }
    return (c.height - HEXEDIT_TOOLBAR_H - HEXEDIT_HEADER_H - HEXEDIT_STATUS_H) / HEXEDIT_ROW_H;
}

uint32_t hex_total_rows()
{
    if (hex_buf_len == 0)
    {
        return 1;
    }
    return (hex_buf_len + HEXEDIT_BYTES_PER_ROW - 1) / HEXEDIT_BYTES_PER_ROW;
}

void hex_clamp_view()
{
    uint32_t rows = hex_visible_rows();
    uint32_t t = hex_total_rows();
    uint32_t max_off = 0;

    if (t > rows)
    {
        max_off = (t - rows) * HEXEDIT_BYTES_PER_ROW;
    }
    if (hex_view_offset > max_off)
    {
        hex_view_offset = max_off;
    }
    hex_view_offset = (hex_view_offset / HEXEDIT_BYTES_PER_ROW) * HEXEDIT_BYTES_PER_ROW;
}

void hex_ensure_visible()
{
    uint32_t rows = hex_visible_rows();
    uint32_t cr = hex_cursor / HEXEDIT_BYTES_PER_ROW;
    uint32_t vr = hex_view_offset / HEXEDIT_BYTES_PER_ROW;

    if (cr < vr)
    {
        hex_view_offset = cr * HEXEDIT_BYTES_PER_ROW;
    }
    else if (cr >= vr + rows)
    {
        hex_view_offset = (cr - rows + 1) * HEXEDIT_BYTES_PER_ROW;
    }
    hex_clamp_view();
}

void hex_move_cursor(int32_t d)
{
    int64_t n = int64_t(hex_cursor) + d;

    if (n < 0)
    {
        n = 0;
    }
    if (n > int64_t(hex_buf_len))
    {
        n = hex_buf_len;
    }

    hex_cursor = uint32_t(n);
    hex_high_nibble = true;
    hex_ensure_visible();
}

bool hex_insert_byte(uint32_t off, uint8_t v)
{
    if (off > hex_buf_len)
    {
        off = hex_buf_len;
    }
    if (!hex_reserve(hex_buf_len + 1))
    {
        return false;
    }
    for (uint32_t i = hex_buf_len; i > off; --i)
    {
        hex_buf[i] = hex_buf[i - 1];
    }
    hex_buf[off] = v;
    hex_buf_len++;
    return true;
}

void hex_delete_byte(uint32_t off)
{
    if (off >= hex_buf_len)
    {
        return;
    }
    for (uint32_t i = off; i + 1 < hex_buf_len; ++i)
    {
        hex_buf[i] = hex_buf[i + 1];
    }
    hex_buf_len--;
    if (hex_cursor > hex_buf_len)
    {
        hex_cursor = hex_buf_len;
    }
}

uint32_t hex_parse_hex(const char* s, bool* ok)
{
    uint32_t v = 0;
    *ok = false;

    if (!s || !*s)
    {
        return 0;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        s += 2;
    }
    if (!*s)
    {
        return 0;
    }
    while (*s)
    {
        int hv = hex_val(*s);
        if (hv < 0)
        {
            return 0;
        }
        if (v > (UINT32_MAX >> 4))
        {
            return 0;
        }
        v = (v << 4) | uint32_t(hv);
        ++s;
    }
    *ok = true;
    return v;
}

bool hex_load_file(const char* path)
{
    extern char hex_dir_path[128];
    extern HexDirEntry hex_entries[64];
    extern int32_t hex_entry_count;
    extern int32_t hex_sel_entry;
    void hex_refresh_dir();
    void hex_normalize_dir(char* out, uint32_t cap, const char* src);

    int32_t d = pf_open(path);

    if (d < 0)
    {
        hex_buf_len = 0;
        hex_cursor = 0;
        hex_view_offset = 0;
        hex_high_nibble = true;
        hex_dirty = false;
        pc_copy(hex_file_path, path, sizeof(hex_file_path));

        char tmp[128];
        pc_copy(tmp, path, sizeof(tmp));
        int l = pc_strlen(tmp);
        for (int i = l - 1; i >= 0; --i)
        {
            if (tmp[i] == '/')
            {
                tmp[i] = '\0';
                if (tmp[0] == '\0')
                {
                    pc_copy(tmp, "/", sizeof(tmp));
                }
                break;
            }
        }
        hex_normalize_dir(hex_dir_path, sizeof(hex_dir_path), tmp);
        hex_refresh_dir();
        hex_set_status("New file", false);
        return true;
    }

    hex_buf_len = 0;
    hex_cursor = 0;
    hex_view_offset = 0;
    hex_high_nibble = true;
    hex_dirty = false;

    if (!hex_reserve(4096))
    {
        pf_close(d);
        return false;
    }

    for (;;)
    {
        uint8_t chunk[512];
        int32_t c = pf_read(d, chunk, sizeof(chunk));
        if (c < 0)
        {
            pf_close(d);
            return false;
        }
        if (c == 0)
        {
            break;
        }
        if (!hex_reserve(hex_buf_len + uint32_t(c)))
        {
            pf_close(d);
            return false;
        }
        for (int i = 0; i < c; ++i)
        {
            hex_buf[hex_buf_len++] = chunk[i];
        }
    }

    pf_close(d);
    pc_copy(hex_file_path, path, sizeof(hex_file_path));

    char tmp[128];
    pc_copy(tmp, path, sizeof(tmp));
    int l = pc_strlen(tmp);
    for (int i = l - 1; i >= 0; --i)
    {
        if (tmp[i] == '/')
        {
            tmp[i] = '\0';
            if (tmp[0] == '\0')
            {
                pc_copy(tmp, "/", sizeof(tmp));
            }
            break;
        }
    }
    hex_normalize_dir(hex_dir_path, sizeof(hex_dir_path), tmp);
    hex_refresh_dir();

    const char* base = path;
    for (const char* p = path; *p; ++p)
    {
        if (*p == '/')
        {
            base = p + 1;
        }
    }
    for (int i = 0; i < hex_entry_count; ++i)
    {
        if (pc_strcmp(hex_entries[i].name, base) == 0)
        {
            hex_sel_entry = i;
        }
    }

    hex_set_status("", false);
    return true;
}

bool hex_save_file()
{
    if (!hex_file_path[0])
    {
        return false;
    }
    if (pf_write_file(hex_file_path, hex_buf, hex_buf_len) < 0)
    {
        return false;
    }
    hex_dirty = false;
    return true;
}