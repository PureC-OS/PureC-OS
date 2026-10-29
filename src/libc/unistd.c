// PureC hosted libc: POSIX fd layer over the FILE preload model.
// fds 0/1/2 are the console; 3..31 map onto FILE* slots. All positioning
// goes through fseek, so lseek works on every regular file.

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

// FILE layout (path field) is internal to stdio.c; fstat reaches it
// through this accessor to avoid a header cycle.

// No kernel cwd: resolve relative paths against logical $PWD so ported
// programs (tcc, etc.) work with "file.c" instead of only "/abs/path".
static const char *resolve_user_path(const char *path, char *buf, size_t cap) {
    (void)cap;
    if (!path || !path[0] || path[0] == '/' ||
        (path[0] == '-' && path[1] == '\0'))
        return path;
    if (!buf) return path;
    if (!realpath(path, buf)) return path;
    return buf;
}

static int syscall_result(int64_t result) {
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

int stdio_stream_fd(const FILE *stream);
FILE *stdio_open_fd(int fd, const char *mode);
void stdio_stdin_reset(void);

int open(const char *path, int flags, ...) {
    if (!path) { errno = EINVAL; return -1; }
    return syscall_result(pc_syscall(SYS_FD_OPEN, (uint64_t)(uintptr_t)path,
                                     (uint64_t)(uint32_t)flags, 0));
}

int creat(const char *path, int mode) {
    (void)mode;
    return open(path, O_WRONLY | O_CREAT | O_TRUNC);
}

ssize_t read(int fd, void *buffer, size_t count) {
    if (!buffer && count) { errno = EINVAL; return -1; }
    if (!count) return 0;
    int64_t result = pc_syscall(SYS_FD_READ, (uint64_t)fd, (uint64_t)(uintptr_t)buffer, count);
    if (result == -KERR_CONSOLE) {
        uint8_t *out = (uint8_t *)buffer;
        size_t done = 0;
        while (done < count) {
            int c = fgetc(stdin);
            if (c == EOF) break;
            out[done++] = (uint8_t)c;
            if (c == '\n') break;
        }
        return (ssize_t)done;
    }
    return (ssize_t)syscall_result(result);
}

ssize_t write(int fd, const void *buffer, size_t count) {
    if (!buffer && count) { errno = EINVAL; return -1; }
    if (!count) return 0;
    return (ssize_t)syscall_result(pc_syscall(SYS_FD_WRITE, (uint64_t)fd,
                                              (uint64_t)(uintptr_t)buffer, count));
}

int close(int fd) {
    int result = syscall_result(pc_syscall(SYS_FILE_CLOSE, (uint64_t)fd, 0, 0) < 0 ? -KERR_BADF : 0);
    if (result == 0 && fd == 0) stdio_stdin_reset();
    return result;
}

FILE *fdopen(int fd, const char *mode) {
    return stdio_open_fd(fd, mode ? mode : "r");
}

off_t lseek(int fd, off_t offset, int origin) {
    int64_t result = pc_syscall(SYS_FD_SEEK, (uint64_t)fd, (uint64_t)(int64_t)offset,
                                (uint64_t)(uint32_t)origin);
    if (result < 0) { errno = (int)-result; return -1; }
    return (off_t)result;
}

int ftruncate(int fd, off_t length) {
    if (length < 0) { errno = EINVAL; return -1; }
    return syscall_result(pc_syscall(SYS_FD_TRUNCATE, (uint64_t)fd, (uint64_t)length, 0));
}

int dup(int fd) {
    return syscall_result(pc_syscall(SYS_DUP, (uint64_t)fd, 0, 0));
}

int dup2(int old_fd, int new_fd) {
    int result = syscall_result(pc_syscall(SYS_DUP2, (uint64_t)old_fd, (uint64_t)new_fd, 0));
    if (result == 0 || new_fd == 0) stdio_stdin_reset();
    return result;
}

int pipe(int fds[2]) {
    if (!fds) { errno = EFAULT; return -1; }
    return syscall_result(pc_syscall(SYS_PIPE, (uint64_t)(uintptr_t)fds, 0, 0));
}

int pipe2(int fds[2], int flags) {
    if (!fds) { errno = EFAULT; return -1; }
    return syscall_result(pc_syscall(SYS_PIPE, (uint64_t)(uintptr_t)fds,
                                     (uint64_t)(uint32_t)flags, 0));
}

int fcntl(int fd, int command, ...) {
    va_list arguments;
    va_start(arguments, command);
    long argument = va_arg(arguments, long);
    va_end(arguments);
    switch (command) {
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

static void fill_stat(struct stat *info, uint64_t size, bool is_dir) {
    memset(info, 0, sizeof(*info));
    info->st_size = size;
    info->st_mode = is_dir ? S_IFDIR : S_IFREG;
}

static void fill_stat_ex(struct stat *info, const struct file_stat_ex *raw) {
    memset(info, 0, sizeof(*info));
    info->st_size = raw->size;
    info->st_mode = (raw->is_directory ? S_IFDIR : S_IFREG) | (raw->mode & 07777);
    info->st_uid = raw->uid;
    info->st_gid = raw->gid;
    info->st_atime = (long)raw->atime;
    info->st_mtime = (long)raw->mtime;
    info->st_ctime = (long)raw->ctime;
}

int stat(const char *path, struct stat *info) {
    if (!path || !info) { errno = EINVAL; return -1; }
    char abs[512];
    path = resolve_user_path(path, abs, sizeof(abs));
    struct file_stat_ex extended;
    if (pc_file_stat_ex(path, &extended) == 0) {
        fill_stat_ex(info, &extended);
        return 0;
    }
    struct file_stat_info raw;
    if (pc_file_stat(path, &raw) < 0) { errno = ENOENT; return -1; }
    fill_stat(info, raw.size, raw.is_directory != 0);
    return 0;
}

struct dir_impl {
    struct fs_directory_entry_long *entries;
    int count;
    int index;
    struct dirent current;
};

#define DIR_CAPACITY 1024

DIR *opendir(const char *path) {
    if (!path) { errno = EINVAL; return 0; }
    char abs[512];
    path = resolve_user_path(path, abs, sizeof(abs));
    DIR *directory = malloc(sizeof(*directory));
    if (!directory) { errno = ENOMEM; return 0; }
    directory->entries = malloc(sizeof(*directory->entries) * DIR_CAPACITY);
    if (!directory->entries) {
        free(directory);
        errno = ENOMEM;
        return 0;
    }
    int32_t count = pc_directory_list_long(path, directory->entries, DIR_CAPACITY);
    if (count < 0) {
        free(directory->entries);
        free(directory);
        errno = ENOENT;
        return 0;
    }
    directory->count = count;
    directory->index = 0;
    return directory;
}

struct dirent *readdir(DIR *directory) {
    if (!directory || directory->index >= directory->count) return 0;
    const struct fs_directory_entry_long *entry = &directory->entries[directory->index++];
    memset(&directory->current, 0, sizeof(directory->current));
    size_t length = strnlen(entry->name, sizeof(directory->current.d_name) - 1);
    memcpy(directory->current.d_name, entry->name, length);
    directory->current.d_ino = (unsigned long)directory->index;
    if (entry->attributes & FS_ATTRIBUTE_DIRECTORY) directory->current.d_type = DT_DIR;
    else if (entry->attributes & FS_ATTRIBUTE_CHAR_DEVICE) directory->current.d_type = DT_CHR;
    else if (entry->attributes & FS_ATTRIBUTE_BLOCK_DEVICE) directory->current.d_type = DT_BLK;
    else directory->current.d_type = DT_REG;
    return &directory->current;
}

void rewinddir(DIR *directory) {
    if (directory) directory->index = 0;
}

int closedir(DIR *directory) {
    if (!directory) { errno = EBADF; return -1; }
    free(directory->entries);
    free(directory);
    return 0;
}

int fileno(FILE *stream) {
    int fd = stdio_stream_fd(stream);
    if (fd < 0) errno = EBADF;
    return fd;
}

int mkdir(const char *path, unsigned int mode) {
    (void)mode;
    if (!path) { errno = EINVAL; return -1; }
    char abs[512];
    path = resolve_user_path(path, abs, sizeof(abs));
    if (pc_directory_create(path) < 0) { errno = EEXIST; return -1; }
    return 0;
}

int fstat(int fd, struct stat *info) {
    if (!info) { errno = EINVAL; return -1; }
    int64_t kind = pc_syscall(SYS_FD_CTL, (uint64_t)fd, FD_CTL_KIND, 0);
    if (kind < 0) { errno = EBADF; return -1; }
    memset(info, 0, sizeof(*info));
    if (kind == FD_KIND_CONSOLE) { info->st_mode = S_IFCHR | 0620; return 0; }
    if (kind == FD_KIND_PIPE) { info->st_mode = S_IFIFO | 0600; return 0; }
    struct file_stat_info raw;
    if (pc_syscall(SYS_FD_STAT, (uint64_t)fd, (uint64_t)(uintptr_t)&raw, 0) < 0) {
        errno = EBADF;
        return -1;
    }
    fill_stat(info, raw.size, raw.is_directory != 0);
    return 0;
}

int lstat(const char *path, struct stat *info) {
    return stat(path, info); // no symlinks on PureC filesystems
}

int unlink(const char *path) {
    if (!path) { errno = EINVAL; return -1; }
    char abs[512];
    path = resolve_user_path(path, abs, sizeof(abs));
    if (pc_file_delete(path) < 0) { errno = ENOENT; return -1; }
    return 0;
}

int access(const char *path, int mode) {
    if (!path) { errno = EINVAL; return -1; }
    (void)mode;
    char abs[512];
    path = resolve_user_path(path, abs, sizeof(abs));
    struct file_stat_info raw;
    if (pc_file_stat(path, &raw) < 0) { errno = ENOENT; return -1; }
    if ((mode & X_OK) && raw.is_directory == 0) {
        // No execute-bit model; regular files are treated as executable
        // when opened through the loader, so report success for exists.
    }
    return 0;
}

int isatty(int fd) {
    int64_t kind = pc_syscall(SYS_FD_CTL, (uint64_t)fd, FD_CTL_KIND, 0);
    if (kind < 0) { errno = EBADF; return 0; }
    if (kind != FD_KIND_CONSOLE) { errno = ENOTTY; return 0; }
    return 1;
}

unsigned int sleep(unsigned int seconds) {
    uint64_t ms = (uint64_t)seconds * 1000ULL;
    if (ms > 0xFFFFFFFFULL) ms = 0xFFFFFFFFULL;
    pc_sleep((uint32_t)ms);
    return 0;
}
char *getcwd(char *buffer, size_t capacity) {
    if (!buffer || !capacity) { errno = EINVAL; return 0; }
    int64_t result = pc_syscall(SYS_GETCWD, (uint64_t)(uintptr_t)buffer, capacity, 0);
    if (result < 0) { errno = result == -KERR_INVAL ? ERANGE : (int)-result; return 0; }
    return buffer;
}

int chdir(const char *path) {
    if (!path) { errno = EINVAL; return -1; }
    return syscall_result(pc_syscall(SYS_CHDIR, (uint64_t)(uintptr_t)path, 0, 0));
}

// ---- mmap family: ENOSYS until the executable-memory syscall lands ----

void *mmap(void *address, size_t length, int prot, int flags, int fd, long offset) {
    (void)address; (void)length; (void)prot; (void)flags; (void)fd; (void)offset;
    errno = ENOSYS;
    return MAP_FAILED;
}

int mprotect(void *address, size_t length, int prot) {
    (void)address; (void)length; (void)prot;
    errno = ENOSYS;
    return -1;
}

int munmap(void *address, size_t length) {
    (void)address; (void)length;
    errno = ENOSYS;
    return -1;
}

void assert_fail(const char *expression, const char *file, int line) {
    pc_write("assert failed: ");
    pc_write(expression ? expression : "?");
    pc_write(" at ");
    pc_write(file ? file : "?");
    pc_write(":");
    // itoa without stdio (assert may fire before stdio is usable).
    char number[16];
    uint32_t n = 0;
    int value = line < 0 ? -line : line;
    char reversed[16];
    uint32_t r = 0;
    if (value == 0) reversed[r++] = '0';
    while (value && r < sizeof(reversed)) { reversed[r++] = (char)('0' + value % 10); value /= 10; }
    if (line < 0 && n < sizeof(number) - 1) number[n++] = '-';
    while (r && n + 1 < sizeof(number)) number[n++] = reversed[--r];
    number[n] = '\0';
    pc_write(number);
    pc_write("\n");
    pc_exit(134);
}

// No symlinks exist on PureC filesystems, so lexical normalization is
// exact: prepend $PWD for relative paths, collapse // and /./, resolve
// /../ by popping, strip the trailing slash (root kept).
char *realpath(const char *path, char *resolved) {
    if (!path || !path[0]) { errno = EINVAL; return 0; }
    char combined[512];
    size_t n = 0;
    bool truncated = false;
    if (path[0] != '/') {
        char cwd[256];
        if (!getcwd(cwd, sizeof(cwd))) return 0;
        for (size_t i = 0; cwd[i]; i++) {
            if (n + 1 >= sizeof(combined)) { truncated = true; break; }
            combined[n++] = cwd[i];
        }
        if (!truncated && n + 1 < sizeof(combined)
            && !(n > 0 && combined[n - 1] == '/'))
            combined[n++] = '/';
    }
    for (size_t i = 0; !truncated && path[i]; i++) {
        if (n + 1 >= sizeof(combined)) { truncated = true; break; }
        combined[n++] = path[i];
    }
    if (truncated) { errno = ENAMETOOLONG; return 0; }
    combined[n] = '\0';
    // In-place collapse into out.
    char out[512];
    size_t w = 0;
    for (size_t r = 0; combined[r]; ) {
        if (combined[r] == '/') {
            while (combined[r] == '/') r++;
            if (!combined[r]) break; // trailing slash, drop it
            if (combined[r] == '.' && (combined[r + 1] == '/' || !combined[r + 1])) {
                r += combined[r + 1] ? 2 : 1;
                continue;
            }
            if (combined[r] == '.' && combined[r + 1] == '.'
                && (combined[r + 2] == '/' || !combined[r + 2])) {
                r += combined[r + 2] ? 3 : 2;
                while (w > 1 && out[w - 1] != '/') w--; // pop one level
                if (w > 1) w--; // drop the slash too (keep root)
                continue;
            }
            out[w++] = '/';
            continue;
        }
        out[w++] = combined[r++];
        if (w + 1 >= sizeof(out)) { errno = ENAMETOOLONG; return 0; }
    }
    if (!w) out[w++] = '/';
    out[w] = '\0';
    if (resolved) {
        // Sized caller buffer (assumed big enough, like PATH_MAX users).
        memcpy(resolved, out, w + 1);
        return resolved;
    }
    char *fresh = (char *)malloc(w + 1);
    if (!fresh) return 0;
    memcpy(fresh, out, w + 1);
    return fresh;
}

