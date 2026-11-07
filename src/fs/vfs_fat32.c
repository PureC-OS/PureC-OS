#include "vfs_fat32.h"
#include "vfs_mount.h"
#include "fat32/fat32_abi.h"
#include "../io/storage/block_device.h"
#include "../kernel/devices/device_manager.h"
#include "../kernel/diagnostics/klog.h"
#include "../kernel/module/kmod_so.h"
#include "../lib/string.h"

#define FAT32_MODULE_NAME "fat32"
#define FAT32_MODULE_PATH "/lib/modules/fat32.elf"
#define FAT32_MAX_DEVICES 8
#define GPT_MAX_ENTRIES 128

static const uint8_t esp_type_guid[16] = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8,
                                          0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0,
                                          0xC9, 0x3E, 0xC9, 0x3B};

static uint8_t gpt_sector[BLOCK_SECTOR_SIZE] __attribute__((aligned(4096)));

static uint32_t read_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static uint64_t read_u64(const uint8_t *p) {
  return (uint64_t)read_u32(p) | ((uint64_t)read_u32(p + 4) << 32);
}

static uint32_t find_esp(uint32_t device) {
  if (!block_device_raw_run(device, false, 1, gpt_sector, 1))
    return 0;
  if (memcmp(gpt_sector, "EFI PART", 8) != 0)
    return 0;
  uint64_t table = read_u64(gpt_sector + 72);
  uint32_t count = read_u32(gpt_sector + 80);
  uint32_t size = read_u32(gpt_sector + 84);
  if (size < 128 || size > BLOCK_SECTOR_SIZE || table == 0 ||
      table > UINT32_MAX)
    return 0;
  if (count > GPT_MAX_ENTRIES)
    count = GPT_MAX_ENTRIES;
  uint32_t per_sector = BLOCK_SECTOR_SIZE / size;
  for (uint32_t i = 0; i < count; i++) {
    if (i % per_sector == 0 &&
        !block_device_raw_run(device, false,
                              (uint32_t)table + i / per_sector, gpt_sector, 1))
      return 0;
    const uint8_t *entry = gpt_sector + (i % per_sector) * size;
    if (memcmp(entry, esp_type_guid, 16) != 0)
      continue;
    uint64_t first = read_u64(entry + 32);
    if (first && first <= UINT32_MAX)
      return (uint32_t)first;
  }
  return 0;
}

void vfs_fat32_mount_boot(void) {
  static bool tried;
  if (tried)
    return;
  tried = true;
  uint32_t devices = block_device_count();
  if (devices > FAT32_MAX_DEVICES)
    devices = FAT32_MAX_DEVICES;
  uint32_t device = 0;
  uint32_t esp = 0;
  for (; device < devices; device++) {
    esp = find_esp(device);
    if (esp)
      break;
  }
  if (!esp) {
    klog(KLOG_INFO, "fat32: no EFI system partition found");
    return;
  }
  if (!devman_load_module(FAT32_MODULE_NAME, FAT32_MODULE_PATH)) {
    klog(KLOG_WARN, "fat32: module not available, /boot stays on the root fs");
    return;
  }
  uint64_t abi = kmod_get(FAT32_MODULE_NAME, FAT32_SYM_ABI);
  uint64_t attach = kmod_get(FAT32_MODULE_NAME, FAT32_SYM_ATTACH);
  uint64_t ops = kmod_get(FAT32_MODULE_NAME, FAT32_SYM_OPS);
  if (!abi || !attach || !ops ||
      ((fat32_abi_fn)abi)() != FAT32_MODULE_ABI) {
    klog(KLOG_WARN, "fat32: module ABI mismatch");
    return;
  }
  void *context = ((fat32_attach_fn)attach)(device, esp);
  if (!context) {
    klogf(KLOG_WARN, "fat32: dev %u lba %u is not a FAT32 volume", device, esp);
    return;
  }
  if (vfs_mount_replace("/boot", (const struct vfs_fs_ops *)ops, context,
                        "esp", 0) < 0) {
    klog(KLOG_WARN, "fat32: cannot mount /boot");
    return;
  }
  klogf(KLOG_OK, "fat32: ESP (dev %u, lba %u) mounted on /boot (rw)", device,
        esp);
}
