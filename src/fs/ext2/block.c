#include "include/ext2_types.h"
#include "include/ext2_block.h"
#include "include/ext2_inode.h"
#include "../../drivers/storage/block_device.h"
#include "../../kernel/diagnostics/klog.h"

static struct ext2_volume g_vol;
static uint8_t g_sector[BLOCK_SECTOR_SIZE] __attribute__((aligned(2)));
static uint8_t g_block[4096] __attribute__((aligned(2)));
static uint8_t g_block2[4096] __attribute__((aligned(2)));
static uint8_t g_block3[4096] __attribute__((aligned(2)));

struct ext2_volume *ext2_volume(void) {
    return &g_vol;
}

uint8_t *ext2_scratch_sector(void) {
    return g_sector;
}

uint8_t *ext2_scratch_block(void) {
    return g_block;
}

uint8_t *ext2_scratch_block2(void) {
    return g_block2;
}

uint8_t *ext2_scratch_block3(void) {
    return g_block3;
}

uint16_t ext2_read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t ext2_read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void ext2_write_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void ext2_write_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

bool ext2_read_block(uint32_t block, void *out) {
    if (!g_vol.mounted) {
        return false;
    }
    uint32_t spb = g_vol.block_size / BLOCK_SECTOR_SIZE;
    uint32_t lba = g_vol.partition_lba + block * spb;
    uint8_t *dst = (uint8_t *)out;
    for (uint32_t i = 0; i < spb; i++) {
        if (!block_device_read(lba + i, dst + i * BLOCK_SECTOR_SIZE)) {
            return false;
        }
    }
    return true;
}

bool ext2_write_block(uint32_t block, const void *in) {
    uint32_t spb = g_vol.block_size / BLOCK_SECTOR_SIZE;
    uint32_t lba = g_vol.partition_lba + block * spb;
    const uint8_t *src = (const uint8_t *)in;
    for (uint32_t i = 0; i < spb; i++) {
        if (!block_device_write(lba + i, src + i * BLOCK_SECTOR_SIZE)) {
            return false;
        }
    }
    return true;
}

bool ext2_group_write(uint32_t group, const uint8_t *gd) {
    uint32_t gd_block = (g_vol.block_size == 1024) ? 2 : 1;
    uint32_t per = g_vol.block_size / 32;
    uint32_t block = gd_block + group / per;
    uint32_t idx = group % per;
    if (!ext2_read_block(block, g_block)) return false;
    for (int i = 0; i < 32; i++) g_block[idx * 32 + i] = gd[i];
    return ext2_write_block(block, g_block);
}

bool ext2_write_inode(uint32_t ino, const uint8_t *in) {
    if (ino == 0 || ino > g_vol.total_inodes) return false;
    uint32_t group = (ino - 1) / g_vol.inodes_per_group;
    uint32_t index = (ino - 1) % g_vol.inodes_per_group;
    uint8_t gd[32];
    uint32_t gd_block = (g_vol.block_size == 1024) ? 2 : 1;
    uint32_t per = g_vol.block_size / 32;
    uint32_t b = gd_block + group / per;
    uint32_t idx = group % per;
    if (!ext2_read_block(b, g_block)) return false;
    for (int i = 0; i < 32; i++) gd[i] = g_block[idx * 32 + i];
    uint32_t table = ext2_read_u32(gd + 8);
    uint32_t blk_off = index / g_vol.inodes_per_block;
    uint32_t off = (index % g_vol.inodes_per_block) * g_vol.inode_size;
    uint32_t block = table + blk_off;
    if (!ext2_read_block(block, g_block)) return false;
    for (uint32_t i = 0; i < g_vol.inode_size; i++) g_block[off + i] = in[i];
    return ext2_write_block(block, g_block);
}

static uint32_t ext2_metadata_blocks_per_group(void) {
    // bmb + imb + inode tables
    uint32_t itb = (g_vol.inodes_per_group + g_vol.inodes_per_block - 1) / g_vol.inodes_per_block;
    if (itb == 0) itb = 1;
    return 2 + itb; // 1 block bitmap + 1 inode bitmap + itb
}

uint32_t ext2_alloc_block(void) {
    uint32_t groups = g_vol.groups_count;
    if (groups == 0) groups = 1;
    uint32_t meta = ext2_metadata_blocks_per_group();
    for (uint32_t g = 0; g < groups; g++) {
        uint8_t gd[32];
        if (!ext2_group_desc_read(g, gd)) continue;
        uint32_t bmb = ext2_read_u32(gd);
        if (!bmb) continue;
        uint8_t bmp[4096];
        if (!ext2_read_block(bmb, bmp)) continue;
        uint32_t start = (g == 0) ? 135 : meta;
        // for 1k: meta=130, g0 reserved 135 includes superblock+gd
        uint32_t end = g_vol.blocks_per_group;
        if (g == groups - 1) {
            uint32_t rem = g_vol.total_blocks % g_vol.blocks_per_group;
            if (rem) end = rem;
        }
        for (uint32_t b = start; b < end; b++) {
            uint32_t byte = b / 8;
            uint32_t bit = b % 8;
            if (byte >= g_vol.block_size) break;
            if ((bmp[byte] & (1u << bit)) == 0) {
                bmp[byte] |= (1u << bit);
                if (!ext2_write_block(bmb, bmp)) return 0;
                uint16_t free = ext2_read_u16(gd + 12);
                if (free) ext2_write_u16(gd + 12, free - 1);
                ext2_group_write(g, gd);
                uint32_t abs = g * g_vol.blocks_per_group + b;
                uint8_t zero[4096] = {0};
                ext2_write_block(abs, zero);
                return abs;
            }
        }
    }
    klogf(KLOG_ERROR,"ext2_alloc_block: no free block groups=%u total=%u",g_vol.groups_count,g_vol.total_blocks);
    return 0;
}

bool ext2_free_block(uint32_t block) {
    if (block == 0 || block >= g_vol.total_blocks) return false;
    uint32_t g = block / g_vol.blocks_per_group;
    if (g >= g_vol.groups_count) return false;
    uint32_t off = block % g_vol.blocks_per_group;
    uint32_t meta = ext2_metadata_blocks_per_group();
    uint32_t reserved = (g == 0) ? 135 : meta;
    if (off < reserved) {
        klogf(KLOG_WARN,"ext2_free_block: attempt to free reserved block %u", block);
        return false;
    }
    uint8_t gd[32];
    if (!ext2_group_desc_read(g, gd)) return false;
    uint32_t bmb = ext2_read_u32(gd);
    if (!bmb) return false;
    uint8_t bmp[4096];
    if (!ext2_read_block(bmb, bmp)) return false;
    uint32_t byte = off / 8;
    uint32_t bit = off % 8;
    if (byte >= g_vol.block_size) return false;
    if ((bmp[byte] & (1u << bit)) == 0) {
        klogf(KLOG_WARN,"ext2_free_block: block %u already free", block);
        return true;
    }
    bmp[byte] &= (uint8_t)~(1u << bit);
    if (!ext2_write_block(bmb, bmp)) return false;
    uint16_t free = ext2_read_u16(gd + 12);
    ext2_write_u16(gd + 12, free + 1);
    return ext2_group_write(g, gd);
}

uint32_t ext2_alloc_inode(void) {
    uint32_t groups = g_vol.groups_count;
    if (groups == 0) groups = 1;
    for (uint32_t g = 0; g < groups; g++) {
        uint8_t gd[32];
        if (!ext2_group_desc_read(g, gd)) continue;
        uint32_t imb = ext2_read_u32(gd + 4);
        if (!imb) continue;
        uint8_t bmp[4096];
        if (!ext2_read_block(imb, bmp)) continue;
        uint32_t start = (g == 0) ? 11 : 0;
        for (uint32_t i = start; i < g_vol.inodes_per_group; i++) {
            uint32_t byte = i / 8;
            uint32_t bit = i % 8;
            if ((bmp[byte] & (1u << bit)) == 0) {
                bmp[byte] |= (1u << bit);
                if (!ext2_write_block(imb, bmp)) return 0;
                uint16_t free = ext2_read_u16(gd + 14);
                if (free) ext2_write_u16(gd + 14, free - 1);
                ext2_group_write(g, gd);
                return g * g_vol.inodes_per_group + i + 1;
            }
        }
    }
    klogf(KLOG_ERROR,"ext2_alloc_inode: no free inode groups=%u",g_vol.groups_count);
    return 0;
}

bool ext2_free_inode(uint32_t ino) {
    if (ino == 0 || ino > g_vol.total_inodes) return false;
    if (ino <= 11) {
        klogf(KLOG_WARN,"ext2_free_inode: attempt to free reserved inode %u", ino);
        return false;
    }
    uint32_t g = (ino - 1) / g_vol.inodes_per_group;
    uint32_t idx = (ino - 1) % g_vol.inodes_per_group;
    uint8_t gd[32];
    if (!ext2_group_desc_read(g, gd)) return false;
    uint32_t imb = ext2_read_u32(gd + 4);
    if (!imb) return false;
    uint8_t bmp[4096];
    if (!ext2_read_block(imb, bmp)) return false;
    uint32_t byte = idx / 8;
    uint32_t bit = idx % 8;
    if ((bmp[byte] & (1u << bit)) == 0) {
        klogf(KLOG_WARN,"ext2_free_inode: inode %u already free", ino);
        return true;
    }
    bmp[byte] &= (uint8_t)~(1u << bit);
    if (!ext2_write_block(imb, bmp)) return false;
    uint16_t free = ext2_read_u16(gd + 14);
    ext2_write_u16(gd + 14, free + 1);
    if (!ext2_group_write(g, gd)) return false;
    uint8_t zero[256] = {0};
    return ext2_write_inode(ino, zero);
}
