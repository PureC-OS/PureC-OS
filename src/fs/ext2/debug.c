#include "include/ext2_types.h"
#include "include/ext2_block.h"
#include "include/ext2_inode.h"
#include "include/ext2_debug.h"
#include "include/ext2_dir.h"
#include "../../lib/string.h"

static void fill_stat_from_inode(uint32_t ino, const uint8_t *ib, struct ext2_stat_info *out) {
    memset(out, 0, sizeof(*out));
    out->ino = ino;
    out->mode = ext2_read_u16(ib);
    out->uid = ext2_read_u16(ib + 2);
    out->size = ext2_read_u32(ib + 4);
    out->atime = ext2_read_u32(ib + 8);
    out->ctime = ext2_read_u32(ib + 12);
    out->mtime = ext2_read_u32(ib + 16);
    out->dtime = ext2_read_u32(ib + 20);
    out->gid = ext2_read_u16(ib + 24);
    out->links = ext2_read_u16(ib + 26);
    out->blocks = ext2_read_u32(ib + 28);
    out->flags = ext2_read_u32(ib + 32);
    for (int i = 0; i < 15; i++) out->blocks_ptr[i] = ext2_read_u32(ib + 40 + i * 4);
    out->generation = ext2_read_u32(ib + 100);
    out->file_acl = ext2_read_u32(ib + 104);
    out->dir_acl = ext2_read_u32(ib + 108);
}

int32_t ext2_stat_path(const char *path, struct ext2_stat_info *out) {
    if (!path || !out) return -3;
    struct ext2_volume *vol = ext2_volume();
    if (!vol->mounted) return -1;
    uint32_t ino;
    int32_t st = ext2_dir_resolve(path, &ino);
    if (st < 0) return st;
    return ext2_stat_ino(ino, out);
}

int32_t ext2_stat_ino(uint32_t ino, struct ext2_stat_info *out) {
    if (!out) return -3;
    struct ext2_volume *vol = ext2_volume();
    if (!vol->mounted) return -1;
    if (ino == 0 || ino > vol->total_inodes) return -2;
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib)) return -1;
    fill_stat_from_inode(ino, ib, out);
    return 0;
}

int32_t ext2_super_info(struct ext2_super_info *out) {
    if (!out) return -3;
    struct ext2_volume *vol = ext2_volume();
    if (!vol->mounted) return -1;
    memset(out, 0, sizeof(*out));
    out->total_inodes = vol->total_inodes;
    out->total_blocks = vol->total_blocks;
    out->block_size = vol->block_size;
    out->blocks_per_group = vol->blocks_per_group;
    out->inodes_per_group = vol->inodes_per_group;
    out->groups_count = vol->groups_count;
    out->first_data_block = vol->first_data_block;
    out->inodes_per_block = vol->inodes_per_block;
    out->inode_size = vol->inode_size;
    out->magic = EXT2_MAGIC;
    out->partition_lba = vol->partition_lba;
    // compute free counts by summing group descriptors for accuracy
    uint32_t free_blocks = 0, free_inodes = 0;
    for (uint32_t g = 0; g < vol->groups_count && g < 32; g++) {
        uint8_t gd[32];
        if (!ext2_group_desc_read(g, gd)) continue;
        free_blocks += ext2_read_u16(gd + 12);
        free_inodes += ext2_read_u16(gd + 14);
    }
    out->free_blocks = free_blocks;
    out->free_inodes = free_inodes;
    // read superblock for state/errors
    uint8_t *sec = ext2_scratch_sector();
    uint32_t sb_lba = vol->partition_lba + 1024 / BLOCK_SECTOR_SIZE;
    if (block_device_read(sb_lba, sec)) {
        uint32_t off = 1024 % BLOCK_SECTOR_SIZE;
        uint8_t *sb = sec + off;
        // sb may cross sector boundary - handle like super.c but simplified: assume fits
        out->state = ext2_read_u16(sb + 58);
        out->errors = ext2_read_u16(sb + 60);
    }
    return 0;
}

int32_t ext2_file_blocks(const char *path, struct ext2_blocks_info *out) {
    if (!path || !out) return -3;
    struct ext2_volume *vol = ext2_volume();
    if (!vol->mounted) return -1;
    uint32_t ino;
    int32_t st = ext2_dir_resolve(path, &ino);
    if (st < 0) return st;
    return ext2_inode_blocks(ino, out);
}

int32_t ext2_inode_blocks(uint32_t ino, struct ext2_blocks_info *out) {
    if (!out) return -3;
    struct ext2_volume *vol = ext2_volume();
    if (!vol->mounted) return -1;
    if (ino == 0 || ino > vol->total_inodes) return -2;
    uint8_t ib[256];
    if (!ext2_inode_read(ino, ib)) return -1;
    memset(out, 0, sizeof(*out));
    out->ino = ino;
    uint32_t size = ext2_read_u32(ib + 4);
    uint32_t bcnt = (size + vol->block_size - 1) / vol->block_size;
    if (bcnt > 64) bcnt = 64;
    out->logical_count = bcnt;
    for (uint32_t i = 0; i < bcnt; i++) {
        out->blocks[i] = ext2_inode_block_ptr(ib, i);
    }
    return 0;
}
