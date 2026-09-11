#include "include/ext2_types.h"
#include "include/ext2_block.h"
#include "include/ext2_super.h"
#include "include/ext2_file.h"
#include "../../drivers/storage/block_device.h"
#include "../../lib/string.h"

bool ext2_super_read(uint32_t partition_lba) {
    struct ext2_volume *vol = ext2_volume();
    uint8_t *sector = ext2_scratch_sector();
    uint32_t sb_lba = partition_lba + 1024 / BLOCK_SECTOR_SIZE;
    uint32_t off = 1024 % BLOCK_SECTOR_SIZE;
    if (!block_device_read(sb_lba, sector)) {
        return false;
    }
    uint8_t *sb = sector + off;
    uint8_t sb_full[1024];
    if (off + 512 > BLOCK_SECTOR_SIZE) {
        uint8_t second[BLOCK_SECTOR_SIZE];
        if (!block_device_read(sb_lba + 1, second)) {
            return false;
        }
        memcpy(sb_full, sb, BLOCK_SECTOR_SIZE - off);
        memcpy(sb_full + (BLOCK_SECTOR_SIZE - off), second, off + 512 - BLOCK_SECTOR_SIZE);
        sb = sb_full;
    }
    uint16_t magic = ext2_read_u16(sb + 56);
    if (magic != EXT2_MAGIC) {
        return false;
    }
    uint32_t log_block_size = ext2_read_u32(sb + 24);
    uint32_t blocks_per_group = ext2_read_u32(sb + 32);
    uint32_t inodes_per_group = ext2_read_u32(sb + 40);
    uint32_t total_blocks = ext2_read_u32(sb + 4);
    uint32_t total_inodes = ext2_read_u32(sb + 0);
    uint32_t first_data_block = ext2_read_u32(sb + 20);
    uint16_t inode_size = ext2_read_u16(sb + 88);
    if (inode_size == 0) {
        inode_size = 128;
    }
    uint32_t block_size = 1024U << log_block_size;
    if (block_size < 1024 || block_size > 4096) {
        return false;
    }
    if (blocks_per_group == 0 || inodes_per_group == 0) {
        return false;
    }
    vol->partition_lba = partition_lba;
    vol->block_size = block_size;
    vol->blocks_per_group = blocks_per_group;
    vol->inodes_per_group = inodes_per_group;
    vol->total_blocks = total_blocks;
    vol->total_inodes = total_inodes;
    vol->first_data_block = first_data_block;
    vol->inode_size = inode_size;
    vol->groups_count = (total_blocks + blocks_per_group - 1) / blocks_per_group;
    if (vol->groups_count == 0) {
        vol->groups_count = 1;
    }
    uint32_t ipb = block_size / inode_size;
    vol->inodes_per_block = ipb ? ipb : 1;
    if (vol->groups_count > 300000) {
        return false;
    }
    return true;
}

bool ext2_super_mount_at(uint32_t lba) {
    if (!ext2_super_read(lba)) {
        return false;
    }
    struct ext2_volume *vol = ext2_volume();
    vol->mounted = true;
    ext2_file_handles_reset();
    return true;
}
