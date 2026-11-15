#include "include/ext2_types.h"
#include "include/ext2_bcache.h"
#include "include/ext2_block.h"
#include "include/ext2_super.h"
#include "include/ext2_format.h"
#include "../../io/storage/block_device.h"
#include "../../lib/string.h"
#include "../../kernel/random/krandom.h"

int32_t ext2_format_at(uint32_t part_lba, uint32_t part_sectors);

static uint8_t zero_chunk[32 * 512];

static bool write_zero_sectors(uint32_t lba, uint32_t count)
{
    while (count)
    {
        uint32_t n = count > 32 ? 32 : count;
        if (!block_device_write_sectors(lba, zero_chunk, n))
            return false;
        lba += n;
        count -= n;
    }
    return true;
}
static int32_t check_target(const char *device_name, const char *serial_confirmation,
                            const char *erase_confirmation, int32_t *index)
{
    if (!device_name || !serial_confirmation || !erase_confirmation)
        return -3;
    int32_t idx = block_device_find(device_name);
    if (idx < 0)
        return -2;
    struct storage_device_info info;
    if (!block_device_get_info((uint32_t)idx, &info))
        return -3;
    if (!info.operational || !info.writable)
        return -10;
    if (!info.serial[0] || strcmp(info.serial, serial_confirmation) != 0 ||
        strcmp(erase_confirmation, "ERASE") != 0)
        return -11;
    if (info.sector_size != BLOCK_SECTOR_SIZE)
        return -8;
    *index = idx;
    return 0;
}

static bool root_on(const char *device_name)
{
    struct ext2_volume *vol = ext2_volume();
    return vol->mounted && strcmp(block_device_name(), device_name) == 0;
}

static int32_t format_selected(int32_t idx, uint32_t lba, uint32_t sectors)
{
    int32_t previous = block_device_find(block_device_name());
    ext2_bcache_reset(true);
    if (!block_device_select((uint32_t)idx))
        return -3;
    int32_t result = ext2_format_at(lba, sectors);
    if (previous >= 0)
        (void)block_device_select((uint32_t)previous);
    return result;
}

int32_t ext2_format_device_impl(const char *device_name, const char *serial_confirmation, const char *erase_confirmation)
{
    int32_t idx;
    int32_t rc = check_target(device_name, serial_confirmation, erase_confirmation, &idx);
    if (rc < 0)
        return rc;
    if (root_on(device_name))
        return -12;
    struct storage_device_info info;
    if (!block_device_get_info((uint32_t)idx, &info))
        return -3;
    if (info.sector_count / (1024 / BLOCK_SECTOR_SIZE) < 1024)
        return -13;
    return format_selected(idx, 0, (uint32_t)(info.sector_count > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : info.sector_count));
}

static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int32_t ext2_format_partition(const char *device_name, uint32_t number, const char *serial_confirmation,
                              const char *erase_confirmation)
{
    int32_t idx;
    int32_t rc = check_target(device_name, serial_confirmation, erase_confirmation, &idx);
    if (rc < 0)
        return rc;
    static uint8_t sec[BLOCK_SECTOR_SIZE];
    if (!block_device_raw_run((uint32_t)idx, false, 1, sec, 1) || memcmp(sec, "EFI PART", 8) != 0)
        return -14;
    uint64_t entries_lba = le64(sec + 72);
    uint32_t entry_count = le32(sec + 80);
    uint32_t entry_size = le32(sec + 84);
    if (entry_size != 128 || !number || number > entry_count || entries_lba > 0xFFFFFFFFULL)
        return -15;
    uint32_t per = BLOCK_SECTOR_SIZE / 128;
    if (!block_device_raw_run((uint32_t)idx, false, (uint32_t)entries_lba + (number - 1) / per, sec, 1))
        return -3;
    const uint8_t *entry = sec + ((number - 1) % per) * 128;
    bool used = false;
    for (int i = 0; i < 16; i++)
        if (entry[i])
            used = true;
    if (!used)
        return -15;
    uint64_t first = le64(entry + 32), last = le64(entry + 40);
    if (last < first || last > 0xFFFFFFFFULL)
        return -16;
    if (root_on(device_name) && ext2_volume()->partition_lba == (uint32_t)first)
        return -12;
    return format_selected(idx, (uint32_t)first, (uint32_t)(last - first + 1));
}

int32_t ext2_format_at(uint32_t part_lba, uint32_t part_sectors)
{
    if (part_sectors > 0xFFFFFF00)
        part_sectors = 0xFFFFFF00;
    uint32_t total_blocks = part_sectors / (1024 / BLOCK_SECTOR_SIZE);
    if (total_blocks < 128)
        return -13;
    if (total_blocks > 0x7FFFFFFF)
        total_blocks = 0x7FFFFFFF;

    uint32_t blocks_per_group = 8192;
    uint32_t inodes_per_group = 1024;
    uint32_t groups = (total_blocks + blocks_per_group - 1) / blocks_per_group;
    if (groups == 0)
        groups = 1;
    if (groups > 300000)
        groups = 300000;

    uint32_t gd_blocks = (groups * 32 + 1023) / 1024;
    uint32_t bmb0 = 2 + gd_blocks;
    uint32_t imb0 = bmb0 + 1;
    uint32_t itb0 = imb0 + 1;
    uint32_t meta_g0 = 2 + gd_blocks + 1 + 1 + 128;
    uint32_t root_data_block = meta_g0;
    uint32_t meta_g0_alloc = meta_g0 + 1;

    uint32_t meta_blocks_other = 130;

    uint64_t free_blocks_tmp = 0;
    for (uint32_t g = 0; g < groups; g++)
    {
        uint32_t grp_blocks = blocks_per_group;
        if (g == groups - 1)
        {
            uint32_t rem = total_blocks % blocks_per_group;
            if (rem)
                grp_blocks = rem;
        }
        uint32_t meta = (g == 0) ? meta_g0_alloc : meta_blocks_other;
        if (grp_blocks > meta)
            free_blocks_tmp += (grp_blocks - meta);
    }
    if (free_blocks_tmp > UINT32_MAX)
        free_blocks_tmp = UINT32_MAX;

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
    ext2_write_u32(sb + 76, 1);
    ext2_write_u32(sb + 84, 11);
    ext2_write_u16(sb + 88, 128);
    ext2_write_u32(sb + 92, 1);
    krandom_fill(sb + 104, 16);
    sb[104 + 6] = (uint8_t)((sb[104 + 6] & 0x0F) | 0x40);
    sb[104 + 8] = (uint8_t)((sb[104 + 8] & 0x3F) | 0x80);

    uint8_t *blk = ext2_scratch_block();

    uint32_t sb_lba = part_lba + 2;
    if (!block_device_write_sectors(sb_lba, sb, 2))
        return -1;

    uint32_t current_g = 0;
    for (uint32_t b = 0; b < gd_blocks; b++)
    {
        memset(blk, 0, 1024);
        for (uint32_t i = 0; i < 32 && current_g < groups; i++, current_g++)
        {
            uint32_t bmb, imb, itb;
            uint16_t free_b, free_i;
            if (current_g == 0)
            {
                bmb = bmb0;
                imb = imb0;
                itb = itb0;
                free_b = (blocks_per_group > meta_g0_alloc) ? (uint16_t)(blocks_per_group - meta_g0_alloc) : 0;
                free_i = (uint16_t)(inodes_per_group - 11);
            }
            else
            {
                bmb = current_g * blocks_per_group;
                imb = bmb + 1;
                itb = bmb + 2;
                uint32_t grp_blocks = blocks_per_group;
                if (current_g == groups - 1)
                {
                    uint32_t rem = total_blocks % blocks_per_group;
                    if (rem)
                        grp_blocks = rem;
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
            if (current_g == 0)
                ext2_write_u16(entry + 16, 2);
        }
        uint32_t gd_lba = part_lba + 4 + b * 2;
        if (!block_device_write_sectors(gd_lba, blk, 2))
            return -1;
    }
    memset(blk, 0, 1024);
    for (uint32_t bit = 0; bit < meta_g0_alloc; bit++)
    {
        blk[bit / 8] |= (uint8_t)(1 << (bit % 8));
    }
    uint32_t bm0_lba = part_lba + bmb0 * 2;
    if (!block_device_write_sectors(bm0_lba, blk, 2))
        return -1;
    memset(blk, 0, 1024);
    for (int i = 0; i < 11; i++)
    {
        blk[i / 8] |= (uint8_t)(1 << (i % 8));
    }
    uint32_t ibm0_lba = part_lba + imb0 * 2;
    if (!block_device_write_sectors(ibm0_lba, blk, 2))
        return -1;
    for (uint32_t b = 0; b < 128; b++)
    {
        if (b == 0)
        {
            memset(blk, 0, 1024);
            uint8_t *ino2 = blk + 128;
            ext2_write_u16(ino2 + 0, 0x41ED);
            ext2_write_u32(ino2 + 4, 1024);
            ext2_write_u32(ino2 + 28, 2);
            ext2_write_u32(ino2 + 40, root_data_block);
            ext2_write_u16(ino2 + 26, 2);
            uint32_t lba = part_lba + (itb0 + b) * 2;
            if (!block_device_write_sectors(lba, blk, 2))
                return -1;
        }
    }
    if (!write_zero_sectors(part_lba + (itb0 + 1) * 2, 127 * 2))
        return -1;

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
    if (!block_device_write_sectors(root_lba, blk, 2))
        return -1;

    for (uint32_t g = 1; g < groups; g++)
    {
        uint32_t bmb = g * blocks_per_group;
        uint32_t imb = bmb + 1;
        uint32_t itb = bmb + 2;

        memset(blk, 0, 1024);
        for (uint32_t b = 0; b < meta_blocks_other; b++)
            blk[b / 8] |= (uint8_t)(1 << (b % 8));
        if (!block_device_write_sectors(part_lba + bmb * 2, blk, 2))
            return -1;

        if (!write_zero_sectors(part_lba + imb * 2, (1 + 128) * 2))
            return -1;
    }

    if (!block_device_flush())
    {
        return -1;
    }
    memset(ext2_volume(), 0, sizeof(struct ext2_volume));
    return 0;
}
