#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "path.h"
#include "../../libfs/include/purefs.h"
#include "../../drivers/storage/storage_types.h"

#define FILES_ENTRY_CAPACITY 96
#define FILES_DISK_CAPACITY 16

struct files_model {
    char path[FILES_PATH_CAPACITY];
    struct pf_entry entries[FILES_ENTRY_CAPACITY];
    struct storage_device_info disks[FILES_DISK_CAPACITY];
    int32_t entry_count;
    int32_t disk_count;
    int32_t error;
    char fs_type[16];
    char root_device[32];
};

void files_model_init(struct files_model *model);
bool files_model_refresh(struct files_model *model);
bool files_model_enter(struct files_model *model, uint32_t index);
bool files_model_up(struct files_model *model);
bool files_model_create(struct files_model *model, const char *name,
                        bool directory);
bool files_model_delete(struct files_model *model, uint32_t index);
bool files_model_rename(struct files_model *model, uint32_t index,
                        const char *new_name);