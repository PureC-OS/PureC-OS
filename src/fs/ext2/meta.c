#include "include/ext2_types.h"
#include "include/ext2_block.h"
#include "include/ext2_inode.h"
#include "include/ext2_dir.h"
#include "include/ext2_meta.h"
#include "include/ext2_time.h"
#include "include/ext2_file.h"
#include "include/ext2_super.h"
#include "../../lib/string.h"

#define INODE_MODE 0
#define INODE_UID 2
#define INODE_GID 24
#define INODE_LINKS EXT2_INODE_LINKS
#define INODE_UID_HIGH 120
#define INODE_GID_HIGH 122
#define EXT2_TYPE_MASK 0xF000
#define EXT2_PERM_MASK 0x0FFF

uint32_t wallclock_unix_seconds(void);

uint32_t ext2_now(void)
{
    return wallclock_unix_seconds();
}

void ext2_stamp_new(uint8_t *inode)
{
    uint32_t now = ext2_now();
    ext2_write_u32(inode + EXT2_INODE_ATIME, now);
    ext2_write_u32(inode + EXT2_INODE_CTIME, now);
    ext2_write_u32(inode + EXT2_INODE_MTIME, now);
}

void ext2_stamp_modified(uint8_t *inode)
{
    uint32_t now = ext2_now();
    ext2_write_u32(inode + EXT2_INODE_CTIME, now);
    ext2_write_u32(inode + EXT2_INODE_MTIME, now);
}

void ext2_stamp_changed(uint8_t *inode)
{
    ext2_write_u32(inode + EXT2_INODE_CTIME, ext2_now());
}

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
    out->symlink = (mode & EXT2_TYPE_MASK) == EXT2_S_IFLNK;
    out->uid = ext2_read_u16(ib + INODE_UID) |
               ((uint32_t)ext2_read_u16(ib + INODE_UID_HIGH) << 16);
    out->gid = ext2_read_u16(ib + INODE_GID) |
               ((uint32_t)ext2_read_u16(ib + INODE_GID_HIGH) << 16);
    out->size = ext2_read_u32(ib + 4);
    out->atime = ext2_read_u32(ib + EXT2_INODE_ATIME);
    out->mtime = ext2_read_u32(ib + EXT2_INODE_MTIME);
    out->ctime = ext2_read_u32(ib + EXT2_INODE_CTIME);
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
    ext2_stamp_changed(ib);
    return ext2_write_inode(ino, ib) ? 0 : -1;
}

int32_t ext2_meta_set_times(const char *path, uint32_t atime, uint32_t mtime)
{
    uint32_t ino;
    int32_t status = ext2_dir_resolve(path, &ino);
    if (status < 0)
        return status;
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib))
        return -1;
    ext2_write_u32(ib + EXT2_INODE_ATIME, atime);
    ext2_write_u32(ib + EXT2_INODE_MTIME, mtime);
    ext2_write_u32(ib + EXT2_INODE_CTIME, ext2_now());
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

#define SUPER_LAST_ORPHAN 232

static int32_t orphan_add(uint32_t ino)
{
    uint32_t head = 0;
    if (!ext2_super_get_u32(SUPER_LAST_ORPHAN, &head))
        head = 0;
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib))
        return -1;
    ext2_write_u32(ib + EXT2_INODE_DTIME, head);
    ext2_stamp_changed(ib);
    if (!ext2_write_inode(ino, ib))
        return -1;
    ext2_super_set_u32(SUPER_LAST_ORPHAN, ino);
    return 0;
}

static void orphan_remove(uint32_t ino)
{
    struct ext2_volume *vol = ext2_volume();
    uint32_t head = 0;
    if (!ext2_super_get_u32(SUPER_LAST_ORPHAN, &head) || !head)
        return;
    uint8_t ib[256];
    if (head == ino)
    {
        uint32_t next = ext2_inode_read(ino, ib) ? ext2_read_u32(ib + EXT2_INODE_DTIME) : 0;
        ext2_super_set_u32(SUPER_LAST_ORPHAN, next <= vol->total_inodes ? next : 0);
        return;
    }
    uint32_t prev = head;
    for (uint32_t guard = 0; prev && guard < vol->total_inodes; guard++)
    {
        if (!ext2_inode_read(prev, ib))
            return;
        uint32_t cur = ext2_read_u32(ib + EXT2_INODE_DTIME);
        if (!cur || cur > vol->total_inodes)
            return;
        if (cur == ino)
        {
            uint8_t victim[256];
            uint32_t next = ext2_inode_read(ino, victim) ? ext2_read_u32(victim + EXT2_INODE_DTIME) : 0;
            ext2_write_u32(ib + EXT2_INODE_DTIME, next <= vol->total_inodes ? next : 0);
            ext2_write_inode(prev, ib);
            return;
        }
        prev = cur;
    }
}

static int32_t free_unlinked(uint32_t ino)
{
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib))
        return -1;
    ext2_inode_free_blocks(ib);
    memset(ib, 0, sizeof(ib));
    ext2_write_u32(ib + EXT2_INODE_DTIME, ext2_now());
    if (!ext2_write_inode(ino, ib))
        return -1;
    return ext2_free_inode(ino) ? 0 : -1;
}

int32_t ext2_inode_release_if_orphan(uint32_t ino)
{
    if (ino <= 11 || ext2_file_inode_open(ino))
        return 0;
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib))
        return -1;
    if (ext2_read_u16(ib + INODE_MODE) == 0 || ext2_read_u16(ib + INODE_LINKS) != 0)
        return 0;
    orphan_remove(ino);
    return free_unlinked(ino);
}

void ext2_orphans_cleanup(void)
{
    struct ext2_volume *vol = ext2_volume();
    uint32_t ino = 0;
    if (!ext2_super_get_u32(SUPER_LAST_ORPHAN, &ino) || !ino)
        return;
    ext2_super_set_u32(SUPER_LAST_ORPHAN, 0);
    for (uint32_t guard = 0; ino && guard < vol->total_inodes; guard++)
    {
        if (ino <= 11 || ino > vol->total_inodes)
            break;
        uint8_t ib[256];
        if (!ext2_inode_read(ino, ib))
            break;
        uint32_t next = ext2_read_u32(ib + EXT2_INODE_DTIME);
        if (ext2_read_u16(ib + INODE_MODE) != 0 && ext2_read_u16(ib + INODE_LINKS) == 0)
            free_unlinked(ino);
        ino = next;
    }
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
    if (links == 0 && !directory && ext2_file_inode_open(ino))
        return orphan_add(ino);
    if (links == 0)
    {
        if (!ext2_inode_read(ino, ib))
            return -1;
        ext2_inode_free_blocks(ib);
        memset(ib, 0, sizeof(ib));
        ext2_write_u32(ib + EXT2_INODE_DTIME, ext2_now());
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

bool ext2_inode_is_fast_symlink(const uint8_t *inode)
{
    if ((ext2_read_u16(inode + INODE_MODE) & EXT2_TYPE_MASK) != EXT2_S_IFLNK)
        return false;
    return ext2_read_u32(inode + 28) == 0 &&
           ext2_read_u32(inode + 4) < EXT2_FAST_LINK_MAX;
}

static int32_t link_target_of(const uint8_t *ib, char *out, uint32_t capacity)
{
    uint32_t size = ext2_read_u32(ib + 4);
    if (size == 0 || size > EXT2_SYMLINK_MAX || size >= capacity)
        return -3;
    if (ext2_inode_is_fast_symlink(ib))
        memcpy(out, ib + 40, size);
    else if (ext2_inode_read_data(ib, 0, out, size) != (int32_t)size)
        return -1;
    out[size] = '\0';
    return (int32_t)size;
}

int32_t ext2_readlink_path(const char *path, char *out, uint32_t capacity)
{
    uint32_t ino;
    int32_t status = ext2_dir_resolve(path, &ino);
    if (status < 0)
        return status;
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib))
        return -1;
    if ((ext2_read_u16(ib + INODE_MODE) & EXT2_TYPE_MASK) != EXT2_S_IFLNK)
        return -3;
    return link_target_of(ib, out, capacity);
}

int32_t ext2_symlink_path(const char *path, const char *target)
{
    char parent[512];
    char base[EXT2_NAME_MAX + 1];
    if (!target || !target[0])
        return -3;
    uint32_t length = (uint32_t)strlen(target);
    if (length > EXT2_SYMLINK_MAX)
        return -3;
    if (!split_path(path, parent, sizeof(parent), base, sizeof(base)) ||
        !name_valid(base))
        return -3;
    uint32_t parent_ino;
    int32_t status = ext2_dir_resolve(parent, &parent_ino);
    if (status < 0)
        return status;
    uint32_t existing;
    if (ext2_dir_find(parent_ino, base, &existing, 0) == 0)
        return -5;
    struct ext2_volume *vol = ext2_volume();
    if (length >= EXT2_FAST_LINK_MAX && length > vol->block_size)
        return -3;
    uint32_t ino = ext2_alloc_inode();
    if (!ino)
        return -4;
    uint8_t ib[256];
    memset(ib, 0, sizeof(ib));
    ext2_write_u16(ib + INODE_MODE, (uint16_t)(EXT2_S_IFLNK | 0777));
    ext2_write_u32(ib + 4, length);
    ext2_write_u16(ib + INODE_LINKS, 1);
    if (length < EXT2_FAST_LINK_MAX)
    {
        memcpy(ib + 40, target, length);
    }
    else
    {
        uint32_t block = ext2_alloc_block();
        if (!block)
        {
            ext2_free_inode(ino);
            return -4;
        }
        uint8_t *data = ext2_scratch_block();
        memset(data, 0, vol->block_size);
        memcpy(data, target, length);
        if (!ext2_write_block(block, data))
        {
            ext2_free_block(block);
            ext2_free_inode(ino);
            return -1;
        }
        ext2_write_u32(ib + 40, block);
        ext2_write_u32(ib + 28, vol->block_size / 512);
    }
    ext2_stamp_new(ib);
    if (!ext2_write_inode(ino, ib))
        return -1;
    status = ext2_dir_add_entry(parent_ino, base, ino, EXT2_FT_SYMLINK);
    if (status < 0)
    {
        ext2_inode_free_blocks(ib);
        ext2_free_inode(ino);
        return status;
    }
    return 0;
}

int32_t ext2_link_walk(const char *path, bool follow_last, char *target,
                       uint32_t capacity, uint32_t *consumed)
{
    struct ext2_volume *vol = ext2_volume();
    if (!vol->mounted || !path)
        return 0;
    const char *p = path;
    uint32_t cur = EXT2_ROOT_INO;
    char comp[EXT2_NAME_MAX + 1];
    while (*p)
    {
        while (*p == '/')
            p++;
        if (!*p)
            return 0;
        uint32_t len = 0;
        while (*p && *p != '/')
        {
            if (len >= EXT2_NAME_MAX)
                return 0;
            comp[len++] = *p++;
        }
        comp[len] = '\0';
        bool last = true;
        for (const char *q = p; *q; q++)
            if (*q != '/')
            {
                last = false;
                break;
            }
        uint32_t next;
        uint8_t type = 0;
        if (ext2_dir_find(cur, comp, &next, &type) < 0)
            return 0;
        uint8_t ib[256];
        if (!ext2_inode_read(next, ib))
            return 0;
        uint16_t kind = ext2_read_u16(ib + INODE_MODE) & EXT2_TYPE_MASK;
        if (kind == EXT2_S_IFLNK && (!last || follow_last))
        {
            int32_t got = link_target_of(ib, target, capacity);
            if (got < 0)
                return got;
            *consumed = (uint32_t)(p - path);
            return 1;
        }
        if (last)
            return 0;
        if (kind != EXT2_S_IFDIR)
            return 0;
        cur = next;
    }
    return 0;
}
