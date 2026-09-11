#include "fs.h"
#include "syscall.h"
#include "../kernel/syscall/syscall.h"

int32_t fs_open(const char *path){
    return (int32_t)userspace_syscall(SYS_OPEN,(uint64_t)path,0,0);
}

int32_t fs_read(int32_t descriptor, void *buffer, uint32_t count){
    return (int32_t)userspace_syscall(SYS_READ,(uint64_t)descriptor,
                                      (uint64_t)buffer,count);
}

int32_t fs_close(int32_t descriptor){
    return (int32_t)userspace_syscall(SYS_CLOSE,(uint64_t)descriptor,0,0);
}

int32_t fs_list(const char *path, struct fs_directory_entry *entries,
                uint32_t capacity){
    return (int32_t)userspace_syscall(SYS_DIR_LIST,(uint64_t)path,
                                      (uint64_t)entries,capacity);
}

int32_t fs_create_file(const char *path){
    return (int32_t)userspace_syscall(SYS_FILE_CREATE,(uint64_t)path,0,0);
}

int32_t fs_write_file(const char *path, const void *buffer, uint32_t count){
    return (int32_t)userspace_syscall(SYS_FILE_WRITE,(uint64_t)path,
                                      (uint64_t)buffer,count);
}

int32_t fs_create_directory(const char *path){
    return (int32_t)userspace_syscall(SYS_MKDIR,(uint64_t)path,0,0);
}

int32_t fs_delete(const char *path){
    return (int32_t)userspace_syscall(SYS_UNLINK,(uint64_t)path,0,0);
}

int32_t fs_rename(const char *path, const char *new_name){
    return (int32_t)userspace_syscall(SYS_RENAME,(uint64_t)path,
                                      (uint64_t)new_name,0);
}

int32_t fs_move(const char *path, const char *destination_directory){
    return (int32_t)userspace_syscall(SYS_FILE_MOVE,(uint64_t)path,
                                      (uint64_t)destination_directory,0);
}
