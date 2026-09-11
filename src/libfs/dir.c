#include "include/purefs.h"
#include "../libc/include/purec.h"
#include "../fs/types/fs_types.h"
#include <stddef.h>

// гарантируем ABI-совместимость pf_entry <-> fs_directory_entry
_Static_assert(sizeof(struct pf_entry)==sizeof(struct fs_directory_entry),
               "pf_entry must match fs_directory_entry");
_Static_assert(sizeof(((struct pf_entry*)0)->name)==sizeof(((struct fs_directory_entry*)0)->name),
               "name capacity mismatch");
_Static_assert(PF_ATTR_DIRECTORY==FS_ATTRIBUTE_DIRECTORY,
               "attr mismatch");

int32_t pf_list(const char *path, struct pf_entry *entries, uint32_t capacity){
    return pc_directory_list(path, (struct fs_directory_entry*)(void*)entries, capacity);
}

int32_t pf_create_dir(const char *path){
    return pc_directory_create(path);
}
