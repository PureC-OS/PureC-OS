#pragma once

#include <stdint.h>

#define FAT32_MODULE_ABI 1u
#define FAT32_SYM_ABI "fat32_abi"
#define FAT32_SYM_ATTACH "fat32_attach"
#define FAT32_SYM_OPS "fat32_ops"

typedef uint32_t (*fat32_abi_fn)(void);
typedef void *(*fat32_attach_fn)(uint32_t device_index, uint32_t partition_lba);
