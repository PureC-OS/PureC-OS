#include "include/ext2_types.h"
#include "include/ext2_block.h"
#include "include/ext2_inode.h"
#include "include/ext2_dir.h"
#include "include/ext2_meta.h"
#include "../../lib/string.h"

#define INODE_MODE 0
#define INODE_UID 2
#define INODE_DTIME 20
#define INODE_GID 24
#define INODE_LINKS 26
#define INODE_UID_HIGH 120
#define INODE_GID_HIGH 122
#define EXT2_TYPE_MASK 0xF000
#define EXT2_PERM_MASK 0x0FFF

static bool split_path(const char *path, char *parent, uint32_t parent_cap,
                       char *base, uint32_t base_cap)
{
    if (!path || path[0] != '/')
        return false;
    uint32_t length = (uint32_t)strlen(path);
    while (length > 1 && path[length - 1] == '/')
        length--;
    uint32_t slash = length;
    while (slash > 0 && path[slash - 1] != '/')
        slash--;
    uint32_t base_len = length - slash;
    if (base_len == 0 || base_len >= base_cap)
        return false;
    uint32_t parent_len = slash > 1 ? slash - 1 : 1;
    if (parent_len >= parent_cap)
        return false;
    memcpy(base, path + slash, base_len);
    base[base_len] = '\0';
    memcpy(parent, path, parent_len);
    parent[parent_len] = '\0';
    return true;
}

static bool name_valid(const char *name)
{
    if (!name || !name[0])
        return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return false;
    uint32_t length = (uint32_t)strlen(name);
    if (length > EXT2_NAME_MAX)
        return false;
    for (uint32_t i = 0; i < length; i++)
        if (name[i] == '/')
            return false;
    return true;
}

int32_t ext2_meta_get(const char *path, struct ext2_meta *out)
{
    uint32_t ino;
    int32_t status = ext2_dir_resolve(path, &ino);
    if (status < 0)
        return status;
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib))
        return -1;
    uint16_t mode = ext2_read_u16(ib + INODE_MODE);
    out->mode = mode & EXT2_PERM_MASK;
    out->directory = (mode & EXT2_TYPE_MASK) == EXT2_S_IFDIR;
    out->uid = ext2_read_u16(ib + INODE_UID) |
               ((uint32_t)ext2_read_u16(ib + INODE_UID_HIGH) << 16);
    out->gid = ext2_read_u16(ib + INODE_GID) |
               ((uint32_t)ext2_read_u16(ib + INODE_GID_HIGH) << 16);
    return 0;
}

int32_t ext2_meta_set(const char *path, int32_t mode, uint32_t uid,
                      uint32_t gid)
{
    uint32_t ino;
    int32_t status = ext2_dir_resolve(path, &ino);
    if (status < 0)
        return status;
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib))
        return -1;
    if (mode >= 0)
    {
        uint16_t old = ext2_read_u16(ib + INODE_MODE);
        ext2_write_u16(ib + INODE_MODE,
                       (uint16_t)((old & EXT2_TYPE_MASK) |
                                  ((uint16_t)mode & EXT2_PERM_MASK)));
    }
    if (uid != EXT2_META_KEEP)
    {
        ext2_write_u16(ib + INODE_UID, (uint16_t)(uid & 0xFFFF));
        ext2_write_u16(ib + INODE_UID_HIGH, (uint16_t)(uid >> 16));
    }
    if (gid != EXT2_META_KEEP)
    {
        ext2_write_u16(ib + INODE_GID, (uint16_t)(gid & 0xFFFF));
        ext2_write_u16(ib + INODE_GID_HIGH, (uint16_t)(gid >> 16));
    }
    return ext2_write_inode(ino, ib) ? 0 : -1;
}

static int32_t dir_remove_entry(uint32_t dir_ino, const char *name)
{
    uint8_t dib[256];
    if (!ext2_inode_read(dir_ino, dib))
        return -1;
    if ((ext2_read_u16(dib) & EXT2_TYPE_MASK) != EXT2_S_IFDIR)
        return -7;
    struct ext2_volume *vol = ext2_volume();
    uint32_t size = ext2_read_u32(dib + 4);
    uint32_t blocks = (size + vol->block_size - 1) / vol->block_size;
    uint32_t nlen = (uint32_t)strlen(name);
    for (uint32_t logical = 0; logical < blocks; logical++)
    {
        uint32_t block = ext2_inode_block_ptr(dib, logical);
        if (!block)
            continue;
        uint8_t *blk = ext2_scratch_block();
        if (!ext2_read_block(block, blk))
            return -1;
        uint32_t offset = 0;
        uint32_t previous = 0xFFFFFFFFu;
        while (offset + 8 <= vol->block_size)
        {
            uint8_t *entry = blk + offset;
            uint32_t ino = ext2_read_u32(entry);
            uint16_t rec = ext2_read_u16(entry + 4);
            uint8_t entry_nlen = entry[6];
            if (rec < 8 || rec > vol->block_size - offset ||
                entry_nlen > rec - 8)
                return -3;
            if (ino != 0 && entry_nlen == nlen &&
                memcmp(entry + 8, name, nlen) == 0)
            {
                if (previous != 0xFFFFFFFFu)
                {
                    uint8_t *before = blk + previous;
                    uint16_t merged = ext2_read_u16(before + 4) + rec;
                    ext2_write_u16(before + 4, merged);
                }
                else
                {
                    ext2_write_u32(entry, 0);
                    entry[6] = 0;
                }
                return ext2_write_block(block, blk) ? 0 : -1;
            }
            previous = offset;
            offset += rec;
        }
    }
    return -2;
}

static int32_t dir_is_empty(uint32_t dir_ino)
{
    uint8_t dib[256];
    if (!ext2_inode_read(dir_ino, dib))
        return -1;
    struct ext2_volume *vol = ext2_volume();
    uint32_t size = ext2_read_u32(dib + 4);
    uint32_t blocks = (size + vol->block_size - 1) / vol->block_size;
    for (uint32_t logical = 0; logical < blocks; logical++)
    {
        uint32_t block = ext2_inode_block_ptr(dib, logical);
        if (!block)
            continue;
        uint8_t *blk = ext2_scratch_block();
        if (!ext2_read_block(block, blk))
            return -1;
        uint32_t offset = 0;
        while (offset + 8 <= vol->block_size)
        {
            uint8_t *entry = blk + offset;
            uint32_t ino = ext2_read_u32(entry);
            uint16_t rec = ext2_read_u16(entry + 4);
            uint8_t entry_nlen = entry[6];
            if (rec < 8 || rec > vol->block_size - offset ||
                entry_nlen > rec - 8)
                return -3;
            if (ino != 0)
            {
                bool dot = entry_nlen == 1 && entry[8] == '.';
                bool dotdot =
                    entry_nlen == 2 && entry[8] == '.' && entry[9] == '.';
                if (!dot && !dotdot)
                    return 0;
            }
            offset += rec;
        }
    }
    return 1;
}

static int32_t adjust_links(uint32_t ino, int32_t delta)
{
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib))
        return -1;
    int32_t links = (int32_t)ext2_read_u16(ib + INODE_LINKS) + delta;
    if (links < 0)
        links = 0;
    ext2_write_u16(ib + INODE_LINKS, (uint16_t)links);
    return ext2_write_inode(ino, ib) ? links : -1;
}

int32_t ext2_unlink_path(const char *path)
{
    char parent[512];
    char base[EXT2_NAME_MAX + 1];
    if (!split_path(path, parent, sizeof(parent), base, sizeof(base)))
        return -3;
    if (!name_valid(base))
        return -3;
    uint32_t parent_ino;
    int32_t status = ext2_dir_resolve(parent, &parent_ino);
    if (status < 0)
        return status;
    uint32_t ino;
    status = ext2_dir_find(parent_ino, base, &ino, 0);
    if (status < 0)
        return status;
    if (ino <= 11)
        return -9;
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib))
        return -1;
    bool directory = (ext2_read_u16(ib) & EXT2_TYPE_MASK) == EXT2_S_IFDIR;
    if (directory)
    {
        int32_t empty = dir_is_empty(ino);
        if (empty < 0)
            return empty;
        if (empty == 0)
            return -9;
    }
    status = dir_remove_entry(parent_ino, base);
    if (status < 0)
        return status;
    if (directory && adjust_links(parent_ino, -1) < 0)
        return -1;
    int32_t links = adjust_links(ino, directory ? -2 : -1);
    if (links < 0)
        return -1;
    if (links == 0)
    {
        if (!ext2_inode_read(ino, ib))
            return -1;
        ext2_inode_free_blocks(ib);
        memset(ib, 0, sizeof(ib));
        ext2_write_u32(ib + INODE_DTIME, 1);
        if (!ext2_write_inode(ino, ib))
            return -1;
        if (!ext2_free_inode(ino))
            return -1;
    }
    return 0;
}

int32_t ext2_rename_path(const char *path, const char *new_name)
{
    char parent[512];
    char base[EXT2_NAME_MAX + 1];
    if (!split_path(path, parent, sizeof(parent), base, sizeof(base)))
        return -3;
    if (!name_valid(base) || !name_valid(new_name))
        return -3;
    if (strcmp(base, new_name) == 0)
        return 0;
    uint32_t parent_ino;
    int32_t status = ext2_dir_resolve(parent, &parent_ino);
    if (status < 0)
        return status;
    uint32_t ino;
    uint8_t type;
    status = ext2_dir_find(parent_ino, base, &ino, &type);
    if (status < 0)
        return status;
    if (ino <= 11)
        return -9;
    if (ext2_dir_find(parent_ino, new_name, 0, 0) == 0)
        return -5;
    status = ext2_dir_add_entry(parent_ino, new_name, ino, type);
    if (status < 0)
        return status;
    return dir_remove_entry(parent_ino, base);
}

static int32_t set_dotdot(uint32_t dir_ino, uint32_t parent_ino)
{
    uint8_t dib[256];
    if (!ext2_inode_read(dir_ino, dib))
        return -1;
    uint32_t block = ext2_inode_block_ptr(dib, 0);
    if (!block)
        return -1;
    uint8_t *blk = ext2_scratch_block();
    if (!ext2_read_block(block, blk))
        return -1;
    uint16_t first = ext2_read_u16(blk + 4);
    if (first < 12 || (uint32_t)first + 8 > ext2_volume()->block_size)
        return -3;
    uint8_t *second = blk + first;
    if (second[6] != 2 || second[8] != '.' || second[9] != '.')
        return -3;
    ext2_write_u32(second, parent_ino);
    return ext2_write_block(block, blk) ? 0 : -1;
}

static bool path_is_inside(const char *path, const char *ancestor)
{
    uint32_t length = (uint32_t)strlen(ancestor);
    while (length > 1 && ancestor[length - 1] == '/')
        length--;
    if (strncmp(path, ancestor, length) != 0)
        return false;
    return path[length] == '\0' || path[length] == '/';
}

int32_t ext2_move_path(const char *path, const char *destination_directory)
{
    char parent[512];
    char base[EXT2_NAME_MAX + 1];
    if (!split_path(path, parent, sizeof(parent), base, sizeof(base)))
        return -3;
    if (!name_valid(base) || !destination_directory ||
        destination_directory[0] != '/')
        return -3;
    uint32_t source_parent;
    int32_t status = ext2_dir_resolve(parent, &source_parent);
    if (status < 0)
        return status;
    uint32_t ino;
    uint8_t type;
    status = ext2_dir_find(source_parent, base, &ino, &type);
    if (status < 0)
        return status;
    if (ino <= 11)
        return -9;
    uint32_t target_dir;
    status = ext2_dir_resolve(destination_directory, &target_dir);
    if (status < 0)
        return status;
    uint8_t tb[256];
    if (!ext2_inode_read(target_dir, tb))
        return -1;
    if ((ext2_read_u16(tb) & EXT2_TYPE_MASK) != EXT2_S_IFDIR)
        return -7;
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib))
        return -1;
    bool directory = (ext2_read_u16(ib) & EXT2_TYPE_MASK) == EXT2_S_IFDIR;
    if (directory && path_is_inside(destination_directory, path))
        return -3;
    if (target_dir == source_parent)
        return 0;
    if (ext2_dir_find(target_dir, base, 0, 0) == 0)
        return -5;
    status = ext2_dir_add_entry(target_dir, base, ino, type);
    if (status < 0)
        return status;
    status = dir_remove_entry(source_parent, base);
    if (status < 0)
        return status;
    if (directory)
    {
        status = set_dotdot(ino, target_dir);
        if (status < 0)
            return status;
        if (adjust_links(source_parent, -1) < 0 ||
            adjust_links(target_dir, 1) < 0)
            return -1;
    }
    return 0;
}
