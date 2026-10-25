#include "hexedit/tree/tree.hpp"
#include "hexedit/buffer/buffer.hpp"

extern "C" {
#include "../../../../libc/include/purec.h"
#include "../../../../libfs/include/purefs.h"
}

char hex_dir_path[128] = "/";
HexDirEntry hex_entries[64];
int32_t hex_entry_count = 0;
int32_t hex_sel_entry = -1;
uint32_t hex_tree_scroll = 0;
uint32_t hex_tree_visible_rows = 0;

bool hex_is_dir_path(const char* p)
{
    struct pf_entry tmp[1];
    int32_t r = pf_list(p, tmp, 1);
    return r >= 0;
}

void hex_normalize_dir(char* out, uint32_t cap, const char* src)
{
    if (!src || !src[0])
    {
        pc_copy(out, "/", cap);
        return;
    }

    pc_copy(out, src, cap);

    uint32_t l = pc_strlen(out);
    if (l > 1 && out[l - 1] == '/')
    {
        out[l - 1] = '\0';
    }

    if (out[0] != '/')
    {
        char tmp[128];
        pc_copy(tmp, out, sizeof(tmp));
        pc_copy(out, "/", cap);
        if (tmp[0])
        {
            uint32_t cur = pc_strlen(out);
            if (cur > 1)
            {
                hex_append_txt(out, "/", cap);
            }
            hex_append_txt(out, tmp, cap);
        }
    }
}

bool hex_join_path(char* out, uint32_t cap, const char* dir, const char* name)
{
    pc_copy(out, dir, cap);

    uint32_t l = pc_strlen(out);
    if (l + 1 + pc_strlen(name) + 1 > cap)
    {
        return false;
    }

    if (l > 1)
    {
        out[l++] = '/';
    }
    else if (out[0] != '/')
    {
        out[0] = '/';
        out[1] = '\0';
        l = 1;
    }

    for (uint32_t i = 0; name[i]; ++i)
    {
        out[l++] = name[i];
    }
    out[l] = '\0';
    return true;
}

void hex_parent_dir(char* out, uint32_t cap, const char* dir)
{
    if (pc_strlen(dir) <= 1)
    {
        pc_copy(out, "/", cap);
        return;
    }

    pc_copy(out, dir, cap);

    uint32_t l = pc_strlen(out);
    while (l > 1 && out[l - 1] == '/')
    {
        out[--l] = '\0';
    }

    for (int i = int(l) - 1; i > 0; --i)
    {
        if (out[i] == '/')
        {
            out[i] = '\0';
            if (out[0] == '\0')
            {
                pc_copy(out, "/", cap);
            }
            return;
        }
    }

    pc_copy(out, "/", cap);
}

void hex_refresh_dir()
{
    hex_entry_count = 0;
    hex_sel_entry = -1;

    struct pf_entry raw[64];
    int32_t r = pf_list(hex_dir_path, raw, 64);

    if (r < 0)
    {
        hex_entry_count = 0;
        return;
    }

    hex_entry_count = r;

    for (int i = 0; i < hex_entry_count; ++i)
    {
        pc_copy(hex_entries[i].name, raw[i].name, sizeof(hex_entries[i].name));
        hex_entries[i].is_dir = pf_is_dir(&raw[i]) != 0;
        hex_entries[i].size = raw[i].size;
    }

    for (int i = 0; i < hex_entry_count; ++i)
    {
        for (int j = i + 1; j < hex_entry_count; ++j)
        {
            bool si = hex_entries[i].is_dir;
            bool sj = hex_entries[j].is_dir;

            if (si != sj && sj)
            {
                HexDirEntry t = hex_entries[i];
                hex_entries[i] = hex_entries[j];
                hex_entries[j] = t;
            }
            else if (si == sj && pc_strcmp(hex_entries[i].name, hex_entries[j].name) > 0)
            {
                HexDirEntry t = hex_entries[i];
                hex_entries[i] = hex_entries[j];
                hex_entries[j] = t;
            }
        }
    }

    hex_tree_scroll = 0;
}