#include "include/ext2_types.h"
#include "include/ext2_block.h"
#include "include/ext2_super.h"
#include "include/ext2_format.h"
#include "../../drivers/storage/block_device.h"
#include "../../lib/string.h"

int32_t ext2_format_at(uint32_t part_lba, uint32_t part_sectors);

int32_t ext2_format_device_impl(const char *device_name, const char *serial_confirmation, const char *erase_confirmation) {
    if (!device_name || !serial_confirmation || !erase_confirmation) {
        return -3;
    }
    int32_t idx = block_device_find(device_name);
    if (idx < 0) {
        return -2;
    }
    struct storage_device_info info;
    if (!block_device_get_info((uint32_t)idx, &info)) {
        return -3;
    }
    if (!info.operational || !info.writable) {
        return -10;
    }
    if (!info.serial[0] || strcmp(info.serial, serial_confirmation) != 0 || strcmp(erase_confirmation, "ERASE") != 0) {
        return -11;
    }
    if (info.sector_size != BLOCK_SECTOR_SIZE) {
        return -8;
    }
    if (!block_device_select((uint32_t)idx)) {
        return -3;
    }
    uint32_t total_blocks = (uint32_t)info.sector_count / (1024 / BLOCK_SECTOR_SIZE);
    if (total_blocks < 1024) {
        return -13;
    }
    return ext2_format_at(0, info.sector_count);
}

int32_t ext2_format_at(uint32_t part_lba, uint32_t part_sectors) {
    if (part_sectors > 0xFFFFFF00) part_sectors = 0xFFFFFF00;
    uint32_t total_blocks = part_sectors / (1024 / BLOCK_SECTOR_SIZE);
    if (total_blocks < 128) return -13;
    if (total_blocks > 0x7FFFFFFF) total_blocks = 0x7FFFFFFF;

    uint32_t blocks_per_group = 8192;
    uint32_t inodes_per_group = 1024;
    uint32_t groups = (total_blocks + blocks_per_group - 1) / blocks_per_group;
    if (groups == 0) groups = 1;
    if (groups > 300000) groups = 300000;

    uint32_t gd_blocks = (groups * 32 + 1023) / 1024;
    uint32_t bmb0 = 2 + gd_blocks;
    uint32_t imb0 = bmb0 + 1;
    uint32_t itb0 = imb0 + 1;
    uint32_t meta_g0 = 2 + gd_blocks + 1 + 1 + 128; // boot, sb, gd, bmb, imb, itb(128)
    uint32_t root_data_block = meta_g0;
    uint32_t meta_g0_alloc = meta_g0 + 1; // plus root data block

    uint32_t meta_blocks_other = 130; // bmb(1) + imb(1) + itb(128)

    uint64_t free_blocks_tmp = 0;
    for (uint32_t g = 0; g < groups; g++) {
        uint32_t grp_blocks = blocks_per_group;
        if (g == groups - 1) {
            uint32_t rem = total_blocks % blocks_per_group;
            if (rem) grp_blocks = rem;
        }
        uint32_t meta = (g == 0) ? meta_g0_alloc : meta_blocks_other;
        if (grp_blocks > meta) free_blocks_tmp += (grp_blocks - meta);
    }
    if (free_blocks_tmp > UINT32_MAX) free_blocks_tmp = UINT32_MAX;

    uint32_t free_inodes_tmp = inodes_per_group * groups - 11;
    uint8_t sb[1024];
    memset(sb, 0, 1024);
    ext2_write_u32(sb + 0, inodes_per_group * groups);
    ext2_write_u32(sb + 4, total_blocks);
    ext2_write_u32(sb + 12, (uint32_t)free_blocks_tmp);
    ext2_write_u32(sb + 16, free_inodes_tmp);
    ext2_write_u32(sb + 20, 1);
    ext2_write_u32(sb + 24, 0);
    ext2_write_u32(sb + 32, blocks_per_group);
    ext2_write_u32(sb + 36, 8192);
    ext2_write_u32(sb + 40, inodes_per_group);
    sb[56] = 0x53;
    sb[57] = 0xEF;
    ext2_write_u16(sb + 58, 1);
    ext2_write_u16(sb + 88, 128);
    ext2_write_u32(sb + 92, 1);

    uint8_t *sec = ext2_scratch_sector();
    uint8_t *blk = ext2_scratch_block();

    uint32_t sb_lba = part_lba + 2;
    memcpy(sec, sb, 512);
    if (!block_device_write(sb_lba, sec)) return -1;
    memcpy(sec, sb + 512, 512);
    if (!block_device_write(sb_lba + 1, sec)) return -1;

    // Write Group Descriptor Table
    uint32_t current_g = 0;
    for (uint32_t b = 0; b < gd_blocks; b++) {
        memset(blk, 0, 1024);
        for (uint32_t i = 0; i < 32 && current_g < groups; i++, current_g++) {
            uint32_t bmb, imb, itb;
            uint16_t free_b, free_i;
            if (current_g == 0) {
                bmb = bmb0;
                imb = imb0;
                itb = itb0;
                free_b = (blocks_per_group > meta_g0_alloc) ? (uint16_t)(blocks_per_group - meta_g0_alloc) : 0;
                free_i = (uint16_t)(inodes_per_group - 11);
            } else {
                bmb = current_g * blocks_per_group;
                imb = bmb + 1;
                itb = bmb + 2;
                uint32_t grp_blocks = blocks_per_group;
                if (current_g == groups - 1) {
                    uint32_t rem = total_blocks % blocks_per_group;
                    if (rem) grp_blocks = rem;
                }
                free_b = (grp_blocks > meta_blocks_other) ? (uint16_t)(grp_blocks - meta_blocks_other) : 0;
                free_i = (uint16_t)inodes_per_group;
            }
            uint8_t *entry = blk + i * 32;
            ext2_write_u32(entry + 0, bmb);
            ext2_write_u32(entry + 4, imb);
            ext2_write_u32(entry + 8, itb);
            ext2_write_u16(entry + 12, free_b);
            ext2_write_u16(entry + 14, free_i);
            if (current_g == 0) ext2_write_u16(entry + 16, 2);
        }
        uint32_t gd_lba = part_lba + 4 + b * 2;
        memcpy(sec, blk, 512);
        if (!block_device_write(gd_lba, sec)) return -1;
        memcpy(sec, blk + 512, 512);
        if (!block_device_write(gd_lba + 1, sec)) return -1;
    }

    // Write Group 0 Block Bitmap
    memset(blk, 0, 1024);
    for (uint32_t bit = 0; bit < meta_g0_alloc; bit++) {
        blk[bit / 8] |= (uint8_t)(1 << (bit % 8));
    }
    uint32_t bm0_lba = part_lba + bmb0 * 2;
    memcpy(sec, blk, 512);
    if (!block_device_write(bm0_lba, sec)) return -1;
    memcpy(sec, blk + 512, 512);
    if (!block_device_write(bm0_lba + 1, sec)) return -1;

    // Write Group 0 Inode Bitmap
    memset(blk, 0, 1024);
    for (int i = 0; i < 11; i++) {
        blk[i / 8] |= (uint8_t)(1 << (i % 8));
    }
    uint32_t ibm0_lba = part_lba + imb0 * 2;
    memcpy(sec, blk, 512);
    if (!block_device_write(ibm0_lba, sec)) return -1;
    memcpy(sec, blk + 512, 512);
    if (!block_device_write(ibm0_lba + 1, sec)) return -1;

    // Write Group 0 Inode Table (128 blocks)
    for (uint32_t b = 0; b < 128; b++) {
        memset(blk, 0, 1024);
        if (b == 0) {
            uint8_t *ino2 = blk + 128; // Inode 2
            ext2_write_u16(ino2 + 0, 0x41ED);
            ext2_write_u32(ino2 + 4, 1024);
            ext2_write_u32(ino2 + 28, 1);
            ext2_write_u32(ino2 + 40, root_data_block);
            ext2_write_u16(ino2 + 24, 1);
        }
        uint32_t lba = part_lba + (itb0 + b) * 2;
        memcpy(sec, blk, 512);
        if (!block_device_write(lba, sec)) return -1;
        memcpy(sec, blk + 512, 512);
        if (!block_device_write(lba + 1, sec)) return -1;
    }

    // Write Root Directory Block
    memset(blk, 0, 1024);
    ext2_write_u32(blk + 0, 2);
    ext2_write_u16(blk + 4, 12);
    blk[6] = 1;
    blk[7] = 2;
    blk[8] = '.';
    ext2_write_u32(blk + 12, 2);
    ext2_write_u16(blk + 16, 1012);
    blk[18] = 2;
    blk[19] = 2;
    blk[20] = '.';
    blk[21] = '.';
    uint32_t root_lba = part_lba + root_data_block * 2;
    memcpy(sec, blk, 512);
    if (!block_device_write(root_lba, sec)) return -1;
    memcpy(sec, blk + 512, 512);
    if (!block_device_write(root_lba + 1, sec)) return -1;

    // Write metadata for groups 1..groups-1
    for (uint32_t g = 1; g < groups; g++) {
        uint32_t bmb = g * blocks_per_group;
        uint32_t imb = bmb + 1;
        uint32_t itb = bmb + 2;

        memset(blk, 0, 1024);
        for (uint32_t b = 0; b < meta_blocks_other; b++) blk[b / 8] |= (uint8_t)(1 << (b % 8));
        memcpy(sec, blk, 512);
        if (!block_device_write(part_lba + bmb * 2, sec)) return -1;
        memcpy(sec, blk + 512, 512);
        if (!block_device_write(part_lba + bmb * 2 + 1, sec)) return -1;

        memset(blk, 0, 1024);
        memcpy(sec, blk, 512);
        if (!block_device_write(part_lba + imb * 2, sec)) return -1;
        memcpy(sec, blk + 512, 512);
        if (!block_device_write(part_lba + imb * 2 + 1, sec)) return -1;

        // Zero out first block of inode table
        if (!block_device_write(part_lba + itb * 2, sec)) return -1;
        if (!block_device_write(part_lba + itb * 2 + 1, sec)) return -1;
    }

    if (!block_device_flush()) {
        return -1;
    }
    memset(ext2_volume(), 0, sizeof(struct ext2_volume));
    return 0;
}
