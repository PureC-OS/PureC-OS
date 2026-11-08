#include "include/hosted/unistd.h"
#include "include/hosted/fcntl.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/errno.h"
#include "include/hosted/sys/wait.h"
#include "include/hosted/stdio.h"
#include "include/hosted/sys/stat.h"
#include "include/hosted/dirent.h"
#include "../fs/types/fs_types.h"
#include "include/hosted/sys/mman.h"
#include "include/purec.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const char *resolve_user_path(const char *path, char *buf, size_t cap)
{
    (void)cap;
    if (!path || !path[0] || path[0] == '/' ||
        (path[0] == '-' && path[1] == '\0'))
        return path;
    if (!buf)
        return path;
    if (!realpath(path, buf))
        return path;
    return buf;
}

static int syscall_result(int64_t result)
{
    if (result < 0)
    {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

int stdio_stream_fd(const FILE *stream);
FILE *stdio_open_fd(int fd, const char *mode);
void stdio_stdin_reset(void);

int open(const char *path, int flags, ...)
{
    if (!path)
    {
        errno = EINVAL;
        return -1;
    }
    return syscall_result(pc_syscall(SYS_FD_OPEN, (uint64_t)(uintptr_t)path,
                                     (uint64_t)(uint32_t)flags, 0));
}

int creat(const char *path, int mode)
{
    (void)mode;
    return open(path, O_WRONLY | O_CREAT | O_TRUNC);
}

ssize_t read(int fd, void *buffer, size_t count)
{
    if (!buffer && count)
    {
        errno = EINVAL;
        return -1;
    }
    if (!count)
        return 0;
    int64_t result = pc_syscall(SYS_FD_READ, (uint64_t)fd, (uint64_t)(uintptr_t)buffer, count);
    if (result == -KERR_CONSOLE)
    {
        if (pc_syscall(SYS_TTY_IOCTL, TTY_RAW_ACTIVE, 0, 0) == 1)
            return (ssize_t)syscall_result(pc_syscall(
                SYS_TTY_READ, (uint64_t)(uintptr_t)buffer, count, 0));
        uint8_t *out = (uint8_t *)buffer;
        size_t done = 0;
        while (done < count)
        {
            int c = fgetc(stdin);
            if (c == EOF)
                break;
            out[done++] = (uint8_t)c;
            if (c == '\n')
                break;
        }
        return (ssize_t)done;
    }
    return (ssize_t)syscall_result(result);
}

ssize_t write(int fd, const void *buffer, size_t count)
{
    if (!buffer && count)
    {
        errno = EINVAL;
        return -1;
    }
    if (!count)
        return 0;
    return (ssize_t)syscall_result(pc_syscall(SYS_FD_WRITE, (uint64_t)fd,
                                              (uint64_t)(uintptr_t)buffer, count));
}

int close(int fd)
{
    int result = syscall_result(pc_syscall(SYS_FILE_CLOSE, (uint64_t)fd, 0, 0) < 0 ? -KERR_BADF : 0);
    if (result == 0 && fd == 0)
        stdio_stdin_reset();
    return result;
}

FILE *fdopen(int fd, const char *mode)
{
    return stdio_open_fd(fd, mode ? mode : "r");
}

off_t lseek(int fd, off_t offset, int origin)
{
    int64_t result = pc_syscall(SYS_FD_SEEK, (uint64_t)fd, (uint64_t)(int64_t)offset,
                                (uint64_t)(uint32_t)origin);
    if (result < 0)
    {
        errno = (int)-result;
        return -1;
    }
    return (off_t)result;
}

int ftruncate(int fd, off_t length)
{
    if (length < 0)
    {
        errno = EINVAL;
        return -1;
    }
    return syscall_result(pc_syscall(SYS_FD_TRUNCATE, (uint64_t)fd, (uint64_t)length, 0));
}

int dup(int fd)
{
    return syscall_result(pc_syscall(SYS_DUP, (uint64_t)fd, 0, 0));
}

int dup2(int old_fd, int new_fd)
{
    int result = syscall_result(pc_syscall(SYS_DUP2, (uint64_t)old_fd, (uint64_t)new_fd, 0));
    if (result == 0 || new_fd == 0)
        stdio_stdin_reset();
    return result;
}

int pipe(int fds[2])
{
    if (!fds)
    {
        errno = EFAULT;
        return -1;
    }
    return syscall_result(pc_syscall(SYS_PIPE, (uint64_t)(uintptr_t)fds, 0, 0));
}

int pipe2(int fds[2], int flags)
{
    if (!fds)
    {
        errno = EFAULT;
        return -1;
    }
    return syscall_result(pc_syscall(SYS_PIPE, (uint64_t)(uintptr_t)fds,
                                     (uint64_t)(uint32_t)flags, 0));
}

int fcntl(int fd, int command, ...)
{
    va_list arguments;
    va_start(arguments, command);
    long argument = va_arg(arguments, long);
    va_end(arguments);
    switch (command)
    {
    case F_GETFD:
        return syscall_result(pc_syscall(SYS_FD_CTL, (uint64_t)fd, FD_CTL_GETFD, 0));
    case F_SETFD:
        return syscall_result(pc_syscall(SYS_FD_CTL, (uint64_t)fd, FD_CTL_SETFD, (uint64_t)argument));
    case F_DUPFD:
        return syscall_result(pc_syscall(SYS_FD_CTL, (uint64_t)fd, FD_CTL_DUPFD, (uint64_t)argument));
    case F_DUPFD_CLOEXEC:
        return syscall_result(pc_syscall(SYS_FD_CTL, (uint64_t)fd, FD_CTL_DUPFD_CLOEXEC, (uint64_t)argument));
    case F_GETFL:
        return syscall_result(pc_syscall(SYS_FD_CTL, (uint64_t)fd, FD_CTL_GETFL, 0));
    case F_SETFL:
        return syscall_result(pc_syscall(SYS_FD_CTL, (uint64_t)fd, FD_CTL_SETFL, (uint64_t)argument));
    default:
        errno = EINVAL;
        return -1;
    }
}

static unsigned long long path_inode(const char *path)
{
    unsigned long long hash = 1469598103934665603ULL;
    for (; path && *path; path++)
    {
        hash ^= (unsigned char)*path;
        hash *= 1099511628211ULL;
    }
    return hash ? hash : 1;
}

static void fill_identity(struct stat *info, const char *path)
{
    info->st_dev = 1;
    info->st_ino = path_inode(path);
    info->st_nlink = 1;
    info->st_blksize = 4096;
}

static void fill_stat(struct stat *info, uint64_t size, bool is_dir)
{
    memset(info, 0, sizeof(*info));
    info->st_size = size;
    info->st_mode = is_dir ? S_IFDIR : S_IFREG;
}

static void fill_stat_ex(struct stat *info, const struct file_stat_ex *raw)
{
    memset(info, 0, sizeof(*info));
    info->st_size = raw->size;
    info->st_mode = (raw->is_directory ? S_IFDIR : S_IFREG) | (raw->mode & 07777);
    info->st_uid = raw->uid;
    info->st_gid = raw->gid;
    info->st_atime = (long)raw->atime;
    info->st_mtime = (long)raw->mtime;
    info->st_ctime = (long)raw->ctime;
}

int stat(const char *path, struct stat *info)
{
    if (!path || !info)
    {
        errno = EINVAL;
        return -1;
    }
    char abs[512];
    path = resolve_user_path(path, abs, sizeof(abs));
    struct file_stat_ex extended;
    if (pc_file_stat_ex(path, &extended) == 0)
    {
        fill_stat_ex(info, &extended);
        fill_identity(info, path);
        return 0;
    }
    struct file_stat_info raw;
    if (pc_file_stat(path, &raw) < 0)
    {
        errno = ENOENT;
        return -1;
    }
    fill_stat(info, raw.size, raw.is_directory != 0);
    fill_identity(info, path);
    return 0;
}

struct dir_impl
{
    struct fs_directory_entry_long *entries;
    int count;
    int index;
    struct dirent current;
    int pseudo_fd;
    char path[512];
};

#define DIRFD_BASE 1000000
#define DIRFD_SLOTS 64
static char dirfd_paths[DIRFD_SLOTS][512];
static int dirfd_used[DIRFD_SLOTS];

#define DIR_CAPACITY 1024

DIR *opendir(const char *path)
{
    if (!path)
    {
        errno = EINVAL;
        return 0;
    }
    char abs[512];
    path = resolve_user_path(path, abs, sizeof(abs));
    DIR *directory = malloc(sizeof(*directory));
    if (!directory)
    {
        errno = ENOMEM;
        return 0;
    }
    directory->entries = malloc(sizeof(*directory->entries) * DIR_CAPACITY);
    if (!directory->entries)
    {
        free(directory);
        errno = ENOMEM;
        return 0;
    }
    int32_t count = pc_directory_list_long(path, directory->entries, DIR_CAPACITY);
    if (count < 0)
    {
        free(directory->entries);
        free(directory);
        errno = ENOENT;
        return 0;
    }
    directory->count = count;
    directory->index = 0;
    directory->pseudo_fd = -1;
    strncpy(directory->path, path, sizeof(directory->path) - 1);
    directory->path[sizeof(directory->path) - 1] = 0;
    for (int slot = 0; slot < DIRFD_SLOTS; slot++)
    {
        if (!dirfd_used[slot])
        {
            dirfd_used[slot] = 1;
            strcpy(dirfd_paths[slot], directory->path);
            directory->pseudo_fd = DIRFD_BASE + slot;
            break;
        }
    }
    return directory;
}

int dirfd(DIR *directory)
{
    if (!directory || directory->pseudo_fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    return directory->pseudo_fd;
}

static int at_path(int dirfd_value, const char *path, char *out, size_t capacity)
{
    if (!path)
    {
        errno = EINVAL;
        return -1;
    }
    if (path[0] == '/' || dirfd_value == AT_FDCWD)
    {
        if (strlen(path) >= capacity)
        {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(out, path);
        return 0;
    }
    int slot = dirfd_value - DIRFD_BASE;
    if (slot < 0 || slot >= DIRFD_SLOTS || !dirfd_used[slot])
    {
        errno = EBADF;
        return -1;
    }
    size_t base = strlen(dirfd_paths[slot]);
    if (base + 1 + strlen(path) >= capacity)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(out, dirfd_paths[slot]);
    if (base && out[base - 1] != '/')
        out[base++] = '/';
    strcpy(out + base, path);
    return 0;
}

int fstatat(int dirfd_value, const char *path, struct stat *info, int flags)
{
    char full[1024];
    if (at_path(dirfd_value, path, full, sizeof(full)) < 0)
        return -1;
    return (flags & AT_SYMLINK_NOFOLLOW) ? lstat(full, info) : stat(full, info);
}

int unlinkat(int dirfd_value, const char *path, int flags)
{
    char full[1024];
    if (at_path(dirfd_value, path, full, sizeof(full)) < 0)
        return -1;
    return (flags & AT_REMOVEDIR) ? rmdir(full) : unlink(full);
}

int openat(int dirfd_value, const char *path, int flags, ...)
{
    char full[1024];
    if (at_path(dirfd_value, path, full, sizeof(full)) < 0)
        return -1;
    va_list arguments;
    va_start(arguments, flags);
    unsigned int mode = (flags & O_CREAT) ? va_arg(arguments, unsigned int) : 0;
    va_end(arguments);
    if (flags & O_DIRECTORY)
    {
        struct stat info;
        if (stat(full, &info) < 0)
            return -1;
        if (!S_ISDIR(info.st_mode))
        {
            errno = ENOTDIR;
            return -1;
        }
        for (int slot = 0; slot < DIRFD_SLOTS; slot++)
        {
            if (!dirfd_used[slot])
            {
                dirfd_used[slot] = 1;
                strcpy(dirfd_paths[slot], full);
                return DIRFD_BASE + slot;
            }
        }
        errno = EMFILE;
        return -1;
    }
    return open(full, flags & ~(O_NOFOLLOW | O_DIRECTORY), mode);
}

DIR *fdopendir(int fd)
{
    int slot = fd - DIRFD_BASE;
    if (slot < 0 || slot >= DIRFD_SLOTS || !dirfd_used[slot])
    {
        errno = EBADF;
        return 0;
    }
    char path[512];
    strcpy(path, dirfd_paths[slot]);
    DIR *directory = opendir(path);
    if (!directory)
        return 0;
    int extra = directory->pseudo_fd - DIRFD_BASE;
    if (extra >= 0 && extra < DIRFD_SLOTS && extra != slot)
        dirfd_used[extra] = 0;
    directory->pseudo_fd = fd;
    return directory;
}

int fchmodat(int dirfd_value, const char *path, unsigned int mode, int flags)
{
    (void)flags;
    char full[1024];
    if (at_path(dirfd_value, path, full, sizeof(full)) < 0)
        return -1;
    return chmod(full, mode);
}

int link(const char *existing, const char *path)
{
    (void)existing;
    (void)path;
    errno = EPERM;
    return -1;
}

long pathconf(const char *path, int name)
{
    (void)path;
    return fpathconf(-1, name);
}

long fpathconf(int fd, int name)
{
    (void)fd;
    switch (name)
    {
    case _PC_LINK_MAX:
        return 1;
    case _PC_MAX_CANON:
    case _PC_MAX_INPUT:
        return 255;
    case _PC_NAME_MAX:
        return 255;
    case _PC_PATH_MAX:
        return 4096;
    case _PC_PIPE_BUF:
        return 4096;
    default:
        errno = EINVAL;
        return -1;
    }
}

int alphasort(const struct dirent **left, const struct dirent **right)
{
    return strcoll((*left)->d_name, (*right)->d_name);
}

int scandir(const char *path, struct dirent ***result,
            int (*select)(const struct dirent *),
            int (*compare)(const struct dirent **, const struct dirent **))
{
    DIR *directory = opendir(path);
    if (!directory)
        return -1;
    struct dirent **list = 0;
    int count = 0, capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != 0)
    {
        if (select && !select(entry))
            continue;
        if (count == capacity)
        {
            int grown = capacity ? capacity * 2 : 32;
            struct dirent **bigger = realloc(list, sizeof(*list) * grown);
            if (!bigger)
                goto fail;
            list = bigger;
            capacity = grown;
        }
        struct dirent *copy = malloc(sizeof(*copy));
        if (!copy)
            goto fail;
        *copy = *entry;
        list[count++] = copy;
    }
    closedir(directory);
    if (compare && count > 1)
        qsort(list, (size_t)count, sizeof(*list), (int (*)(const void *, const void *))compare);
    *result = list;
    return count;
fail:
    for (int i = 0; i < count; i++)
        free(list[i]);
    free(list);
    closedir(directory);
    errno = ENOMEM;
    return -1;
}

struct dirent *readdir(DIR *directory)
{
    if (!directory || directory->index >= directory->count)
        return 0;
    const struct fs_directory_entry_long *entry = &directory->entries[directory->index++];
    memset(&directory->current, 0, sizeof(directory->current));
    size_t length = strnlen(entry->name, sizeof(directory->current.d_name) - 1);
    memcpy(directory->current.d_name, entry->name, length);
    directory->current.d_ino = (unsigned long)directory->index;
    if (entry->attributes & FS_ATTRIBUTE_DIRECTORY)
        directory->current.d_type = DT_DIR;
    else if (entry->attributes & FS_ATTRIBUTE_CHAR_DEVICE)
        directory->current.d_type = DT_CHR;
    else if (entry->attributes & FS_ATTRIBUTE_BLOCK_DEVICE)
        directory->current.d_type = DT_BLK;
    else
        directory->current.d_type = DT_REG;
    return &directory->current;
}

void rewinddir(DIR *directory)
{
    if (directory)
        directory->index = 0;
}

int closedir(DIR *directory)
{
    if (!directory)
    {
        errno = EBADF;
        return -1;
    }
    if (directory->pseudo_fd >= DIRFD_BASE)
        dirfd_used[directory->pseudo_fd - DIRFD_BASE] = 0;
    free(directory->entries);
    free(directory);
    return 0;
}

int fileno(FILE *stream)
{
    int fd = stdio_stream_fd(stream);
    if (fd < 0)
        errno = EBADF;
    return fd;
}

int mkdir(const char *path, unsigned int mode)
{
    (void)mode;
    if (!path)
    {
        errno = EINVAL;
        return -1;
    }
    char abs[512];
    path = resolve_user_path(path, abs, sizeof(abs));
    if (pc_directory_create(path) < 0)
    {
        errno = EEXIST;
        return -1;
    }
    return 0;
}

int fstat(int fd, struct stat *info)
{
    if (!info)
    {
        errno = EINVAL;
        return -1;
    }
    int64_t kind = pc_syscall(SYS_FD_CTL, (uint64_t)fd, FD_CTL_KIND, 0);
    if (kind < 0)
    {
        errno = EBADF;
        return -1;
    }
    memset(info, 0, sizeof(*info));
    info->st_nlink = 1;
    info->st_blksize = 4096;
    if (kind == FD_KIND_CONSOLE)
    {
        info->st_dev = 2;
        info->st_ino = 1;
        info->st_mode = S_IFCHR | 0620;
        return 0;
    }
    if (kind == FD_KIND_PIPE)
    {
        info->st_dev = 3;
        info->st_ino = (unsigned long long)fd + 1;
        info->st_mode = S_IFIFO | 0600;
        return 0;
    }
    struct file_stat_info raw;
    if (pc_syscall(SYS_FD_STAT, (uint64_t)fd, (uint64_t)(uintptr_t)&raw, 0) < 0)
    {
        errno = EBADF;
        return -1;
    }
    fill_stat(info, raw.size, raw.is_directory != 0);
    char path[512];
    if (pc_syscall(SYS_FD_PATH, (uint64_t)fd, (uint64_t)(uintptr_t)path, sizeof(path)) > 0)
        fill_identity(info, path);
    return 0;
}

int lstat(const char *path, struct stat *info)
{
    return stat(path, info);
}

int unlink(const char *path)
{
    if (!path)
    {
        errno = EINVAL;
        return -1;
    }
    char abs[512];
    path = resolve_user_path(path, abs, sizeof(abs));
    if (pc_file_delete(path) < 0)
    {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int access(const char *path, int mode)
{
    if (!path)
    {
        errno = EINVAL;
        return -1;
    }
    (void)mode;
    char abs[512];
    path = resolve_user_path(path, abs, sizeof(abs));
    struct file_stat_info raw;
    if (pc_file_stat(path, &raw) < 0)
    {
        errno = ENOENT;
        return -1;
    }
    if ((mode & X_OK) && raw.is_directory == 0)
    {
    }
    return 0;
}

int isatty(int fd)
{
    int64_t kind = pc_syscall(SYS_FD_CTL, (uint64_t)fd, FD_CTL_KIND, 0);
    if (kind < 0)
    {
        errno = EBADF;
        return 0;
    }
    if (kind != FD_KIND_CONSOLE)
    {
        errno = ENOTTY;
        return 0;
    }
    return 1;
}

unsigned int sleep(unsigned int seconds)
{
    uint64_t ms = (uint64_t)seconds * 1000ULL;
    if (ms > 0xFFFFFFFFULL)
        ms = 0xFFFFFFFFULL;
    pc_sleep((uint32_t)ms);
    return 0;
}
char *getcwd(char *buffer, size_t capacity)
{
    if (!buffer || !capacity)
    {
        errno = EINVAL;
        return 0;
    }
    int64_t result = pc_syscall(SYS_GETCWD, (uint64_t)(uintptr_t)buffer, capacity, 0);
    if (result < 0)
    {
        errno = result == -KERR_INVAL ? ERANGE : (int)-result;
        return 0;
    }
    return buffer;
}

int chdir(const char *path)
{
    if (!path)
    {
        errno = EINVAL;
        return -1;
    }
    return syscall_result(pc_syscall(SYS_CHDIR, (uint64_t)(uintptr_t)path, 0, 0));
}

static int64_t syscall_four(uint64_t number, uint64_t argument1,
                            uint64_t argument2, uint64_t argument3,
                            uint64_t argument4)
{
    int64_t result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "b"(argument1), "c"(argument2),
                       "d"(argument3), "S"(argument4)
                     : "r10", "r8", "memory");
    return result;
}

static void *mmap_address_result(int64_t result)
{
    if (result < 0)
    {
        errno = (int)-result;
        return MAP_FAILED;
    }
    return (void *)(uintptr_t)result;
}

static int mmap_fill_from_file(char *base, size_t length, int fd, long offset)
{
    off_t saved = lseek(fd, 0, SEEK_CUR);
    if (saved < 0)
        return -1;
    if (lseek(fd, (off_t)offset, SEEK_SET) < 0)
        return -1;
    size_t done = 0;
    int failed = 0;
    while (done < length)
    {
        ssize_t got = read(fd, base + done, length - done);
        if (got < 0)
        {
            failed = 1;
            break;
        }
        if (got == 0)
            break;
        done += (size_t)got;
    }
    int saved_errno = errno;
    lseek(fd, saved, SEEK_SET);
    errno = saved_errno;
    return failed ? -1 : 0;
}

void *mmap(void *address, size_t length, int prot, int flags, int fd, long offset)
{
    if (!length || (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)))
    {
        errno = EINVAL;
        return MAP_FAILED;
    }
    int sharing = flags & (MAP_SHARED | MAP_PRIVATE);
    if (sharing != MAP_SHARED && sharing != MAP_PRIVATE)
    {
        errno = EINVAL;
        return MAP_FAILED;
    }
    int anonymous = (flags & MAP_ANONYMOUS) != 0;
    int fixed = (flags & MAP_FIXED) != 0;
    if (fixed && ((uintptr_t)address & 4095u))
    {
        errno = EINVAL;
        return MAP_FAILED;
    }
    int backed = !anonymous && prot != PROT_NONE;
    if (!anonymous)
    {
        if (fd < 0)
        {
            errno = EBADF;
            return MAP_FAILED;
        }
        if (offset < 0 || (offset & 4095))
        {
            errno = EINVAL;
            return MAP_FAILED;
        }
    }
    struct kernel_mmap_args request;
    request.address = fixed ? (uint64_t)(uintptr_t)address : 0;
    request.length = length;
    request.fd = -1;
    request.offset = 0;
    if (sharing == MAP_SHARED && (anonymous || fd >= 0))
    {
        request.prot = (uint64_t)prot;
        request.flags = KMAP_SHARED | (fixed ? KMAP_FIXED : 0);
        if (!anonymous)
        {
            request.fd = fd;
            request.offset = (uint64_t)offset;
        }
        void *shared = mmap_address_result(
            pc_syscall(SYS_MMAP, (uint64_t)(uintptr_t)&request, 0, 0));
        if (shared != MAP_FAILED || anonymous || errno != ENODEV)
            return shared;
        request.fd = -1;
        request.offset = 0;
        if (prot & PROT_WRITE)
        {
            errno = ENODEV;
            return MAP_FAILED;
        }
    }
    request.prot = backed ? (uint64_t)(PROT_READ | PROT_WRITE) : (uint64_t)prot;
    request.flags = fixed ? KMAP_FIXED : 0;
    void *base = mmap_address_result(
        pc_syscall(SYS_MMAP, (uint64_t)(uintptr_t)&request, 0, 0));
    if (base == MAP_FAILED || !backed)
        return base;
    if (mmap_fill_from_file((char *)base, length, fd, offset) < 0)
    {
        int saved_errno = errno;
        munmap(base, length);
        errno = saved_errno;
        return MAP_FAILED;
    }
    if (prot != (PROT_READ | PROT_WRITE) && mprotect(base, length, prot) < 0)
    {
        int saved_errno = errno;
        munmap(base, length);
        errno = saved_errno;
        return MAP_FAILED;
    }
    return base;
}

int mprotect(void *address, size_t length, int prot)
{
    if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
    {
        errno = EINVAL;
        return -1;
    }
    if (!length)
        return 0;
    return syscall_result(pc_syscall(SYS_MPROTECT, (uint64_t)(uintptr_t)address,
                                     length, (uint64_t)prot));
}

int munmap(void *address, size_t length)
{
    return syscall_result(pc_syscall(SYS_MUNMAP, (uint64_t)(uintptr_t)address,
                                     length, 0));
}

void *mremap(void *address, size_t old_length, size_t new_length, int flags, ...)
{
    if (flags & ~MREMAP_MAYMOVE)
    {
        errno = EINVAL;
        return MAP_FAILED;
    }
    return mmap_address_result(syscall_four(
        SYS_MREMAP, (uint64_t)(uintptr_t)address, old_length, new_length,
        (flags & MREMAP_MAYMOVE) ? KMAP_REMAP_MAYMOVE : 0));
}

int msync(void *address, size_t length, int flags)
{
    (void)length;
    if (((uintptr_t)address & 4095u) ||
        ((flags & MS_ASYNC) && (flags & MS_SYNC)))
    {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int madvise(void *address, size_t length, int advice)
{
    if ((uintptr_t)address & 4095u)
    {
        errno = EINVAL;
        return -1;
    }
    switch (advice)
    {
    case MADV_NORMAL:
    case MADV_RANDOM:
    case MADV_SEQUENTIAL:
    case MADV_WILLNEED:
    case MADV_FREE:
    case MADV_DONTFORK:
    case MADV_DOFORK:
    case MADV_HUGEPAGE:
    case MADV_NOHUGEPAGE:
    case MADV_DONTDUMP:
    case MADV_DODUMP:
        return 0;
    case MADV_DONTNEED:
        if (!length)
            return 0;
        return syscall_result(pc_syscall(SYS_MADVISE, (uint64_t)(uintptr_t)address,
                                         length, (uint64_t)advice));
    default:
        errno = EINVAL;
        return -1;
    }
}

int mlock(const void *address, size_t length)
{
    (void)address;
    (void)length;
    return 0;
}

int munlock(const void *address, size_t length)
{
    (void)address;
    (void)length;
    return 0;
}

void assert_fail(const char *expression, const char *file, int line)
{
    pc_write("assert failed: ");
    pc_write(expression ? expression : "?");
    pc_write(" at ");
    pc_write(file ? file : "?");
    pc_write(":");
    char number[16];
    uint32_t n = 0;
    int value = line < 0 ? -line : line;
    char reversed[16];
    uint32_t r = 0;
    if (value == 0)
        reversed[r++] = '0';
    while (value && r < sizeof(reversed))
    {
        reversed[r++] = (char)('0' + value % 10);
        value /= 10;
    }
    if (line < 0 && n < sizeof(number) - 1)
        number[n++] = '-';
    while (r && n + 1 < sizeof(number))
        number[n++] = reversed[--r];
    number[n] = '\0';
    pc_write(number);
    pc_write("\n");
    pc_exit(134);
}

char *realpath(const char *path, char *resolved)
{
    if (!path || !path[0])
    {
        errno = EINVAL;
        return 0;
    }
    char combined[512];
    size_t n = 0;
    bool truncated = false;
    if (path[0] != '/')
    {
        char cwd[256];
        if (!getcwd(cwd, sizeof(cwd)))
            return 0;
        for (size_t i = 0; cwd[i]; i++)
        {
            if (n + 1 >= sizeof(combined))
            {
                truncated = true;
                break;
            }
            combined[n++] = cwd[i];
        }
        if (!truncated && n + 1 < sizeof(combined) && !(n > 0 && combined[n - 1] == '/'))
            combined[n++] = '/';
    }
    for (size_t i = 0; !truncated && path[i]; i++)
    {
        if (n + 1 >= sizeof(combined))
        {
            truncated = true;
            break;
        }
        combined[n++] = path[i];
    }
    if (truncated)
    {
        errno = ENAMETOOLONG;
        return 0;
    }
    combined[n] = '\0';
    char out[512];
    size_t w = 0;
    for (size_t r = 0; combined[r];)
    {
        if (combined[r] == '/')
        {
            while (combined[r] == '/')
                r++;
            if (!combined[r])
                break; // trailing slash, drop it
            if (combined[r] == '.' && (combined[r + 1] == '/' || !combined[r + 1]))
            {
                r += combined[r + 1] ? 2 : 1;
                continue;
            }
            if (combined[r] == '.' && combined[r + 1] == '.' && (combined[r + 2] == '/' || !combined[r + 2]))
            {
                r += combined[r + 2] ? 3 : 2;
                while (w > 1 && out[w - 1] != '/')
                    w--; // pop one level
                if (w > 1)
                    w--;
                continue;
            }
            out[w++] = '/';
            continue;
        }
        out[w++] = combined[r++];
        if (w + 1 >= sizeof(out))
        {
            errno = ENAMETOOLONG;
            return 0;
        }
    }
    if (!w)
        out[w++] = '/';
    out[w] = '\0';
    if (resolved)
    {
        memcpy(resolved, out, w + 1);
        return resolved;
    }
    char *fresh = (char *)malloc(w + 1);
    if (!fresh)
        return 0;
    memcpy(fresh, out, w + 1);
    return fresh;
}
