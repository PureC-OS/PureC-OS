#include "include/purefs.h"
#include "../libc/include/purec.h"
#include <stddef.h>

const char *pf_version(void){
    return PF_VERSION;
}

const char *pf_strerror(int32_t error){
    switch(error){
        case 0: return "ok";
        case PF_ERROR_IO: return "io error";
        case PF_ERROR_NOT_FOUND: return "not found";
        case PF_ERROR_INVALID: return "invalid argument";
        case PF_ERROR_NO_SPACE: return "no space";
        case PF_ERROR_EXISTS: return "already exists";
        case PF_ERROR_NOT_FILE: return "not a file";
        case PF_ERROR_NOT_DIR: return "not a directory";
        case PF_ERROR_UNSUPPORTED: return "unsupported";
        case PF_ERROR_BUSY: return "busy";
        case PF_ERROR_READ_ONLY: return "read only";
        case PF_ERROR_CONFIRMATION: return "confirmation failed";
        case PF_ERROR_NOT_BLANK: return "not blank - already has data";
        case PF_ERROR_TOO_SMALL: return "too small";
        default: return "unknown error";
    }
}

bool pf_is_dir(const struct pf_entry *entry){
    return entry && (entry->attributes & PF_ATTR_DIRECTORY)!=0;
}

bool pf_exists(const char *path){
    return pc_file_exists(path);
}

int32_t pf_open(const char *path){
    return pc_file_open(path);
}

int32_t pf_read(int32_t fd, void *buffer, uint32_t capacity){
    return pc_file_read(fd, buffer, capacity);
}

int32_t pf_close(int32_t fd){
    return pc_file_close(fd);
}

int32_t pf_write_file(const char *path, const void *buffer, uint32_t size){
    return pc_file_write(path, buffer, size);
}

int32_t pf_create_file(const char *path){
    return pc_file_create(path);
}

int32_t pf_delete(const char *path){
    return pc_file_delete(path);
}

int32_t pf_rename(const char *path, const char *new_name){
    return pc_file_rename(path, new_name);
}

int32_t pf_move(const char *path, const char *destination_directory){
    return pc_file_move(path, destination_directory);
}

int32_t pf_append_file(const char *path, const void *buffer, uint32_t size){
    if(!path) return PF_ERROR_INVALID;
    return (int32_t)pc_syscall(SYS_FILE_APPEND,(uint64_t)(uintptr_t)path,(uint64_t)(uintptr_t)buffer,(uint64_t)size);
}

int32_t pf_read_all(const char *path, void *buffer, uint32_t capacity){
    if(!path || !buffer || !capacity) return PF_ERROR_INVALID;
    int32_t fd = pc_file_open(path);
    if(fd < 0) return fd;
    int32_t n = pc_file_read(fd, buffer, capacity);
    (void)pc_file_close(fd);
    return n;
}

int32_t pf_format_device(const char *device, const char *serial, uint8_t fs_type){
    if(!device || !serial) return PF_ERROR_INVALID;
    return (int32_t)pc_syscall(SYS_FORMAT_DEVICE_EX, (uint64_t)(uintptr_t)device, (uint64_t)(uintptr_t)serial, (uint64_t)fs_type);
}

const char *pf_fs_type_name(uint8_t fs_type){
    if(fs_type == PF_FS_EXT2) return "ext2";
    return "fat32";
}

bool pf_fs_supported(uint8_t fs_type){
    return fs_type == PF_FS_FAT32 || fs_type == PF_FS_EXT2;
}
