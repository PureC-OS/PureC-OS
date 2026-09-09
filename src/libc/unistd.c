// PureC hosted libc: POSIX fd layer over the FILE preload model.
// fds 0/1/2 are the console; 3..31 map onto FILE* slots. All positioning
// goes through fseek, so lseek works on every regular file.

#include "include/hosted/unistd.h"
#include "include/hosted/fcntl.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/errno.h"
#include "include/hosted/stdio.h"
#include "include/hosted/sys/stat.h"
#include "include/hosted/sys/mman.h"
#include "include/purec.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UNIX_FD_MAX 32

static FILE *fd_slots[UNIX_FD_MAX];

static int fd_allocate(FILE *stream) {
    for (int fd = 3; fd < UNIX_FD_MAX; fd++) {
        if (!fd_slots[fd]) { fd_slots[fd] = stream; return fd; }
    }
    errno = EMFILE;
    return -1;
}

static FILE *fd_lookup(int fd) {
    if (fd < 3 || fd >= UNIX_FD_MAX || !fd_slots[fd]) {
        errno = EBADF;
        return 0;
    }
    return fd_slots[fd];
}

// FILE layout (path field) is internal to stdio.c; fstat reaches it
// through this accessor to avoid a header cycle.
const char *stdio_stream_path(const FILE *stream);

int open(const char *path, int flags, ...) {
    if (!path) { errno = EINVAL; return -1; }
    bool create = (flags & O_CREAT) != 0;
    bool exclusive = (flags & O_EXCL) != 0;
    bool truncate = (flags & O_TRUNC) != 0;
    bool append = (flags & O_APPEND) != 0;
    int access_mode = flags & O_ACCMODE;

    int32_t probe = pc_file_open(path);
    bool exists = probe >= 0;
    if (exists) pc_file_close(probe);
    if (exists && create && exclusive) { errno = EEXIST; return -1; }
    if (!exists) {
        if (!create) { errno = ENOENT; return -1; }
        if (pc_file_create(path) < 0) { errno = ENOENT; return -1; }
        exists = true;
    }
    if (exists && truncate && access_mode == O_RDONLY) {
        // POSIX allows O_TRUNC|O_RDONLY (truncate without write); emulate.
        static char empty = 0;
        if (pc_file_write(path, &empty, 0) < 0) { errno = EIO; return -1; }
    }

    const char *mode = "r";
    // NOTE: plain O_WRONLY without O_TRUNC maps to "r+" (reads stay
    // technically possible) to avoid ever truncating content that POSIX
    // says must survive. TCC only uses O_WRONLY with O_CREAT|O_TRUNC.
    if (access_mode == O_WRONLY) mode = append ? "a" : (truncate ? "w" : "r+");
    else if (access_mode == O_RDWR) mode = append ? "a+" : (truncate ? "w+" : "r+");
    else mode = "r";

    FILE *stream = fopen(path, mode);
    if (!stream) return -1; // errno already set by fopen
    int fd = fd_allocate(stream);
    if (fd < 0) { fclose(stream); return -1; }
    return fd;
}

int creat(const char *path, int mode) {
    (void)mode;
    return open(path, O_WRONLY | O_CREAT | O_TRUNC);
}

ssize_t read(int fd, void *buffer, size_t count) {
    if (!buffer) { errno = EINVAL; return -1; }
    if (fd == STDIN_FILENO) {
        uint8_t *out = (uint8_t *)buffer;
        size_t done = 0;
        while (done < count) {
            int c = fgetc(stdin);
            if (c == EOF) break;
            out[done++] = (uint8_t)c;
        }
        return (ssize_t)done;
    }
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) { errno = EBADF; return -1; }
    FILE *stream = fd_lookup(fd);
    if (!stream) return -1;
    return (ssize_t)fread(buffer, 1, count, stream);
}

ssize_t write(int fd, const void *buffer, size_t count) {
    if (!buffer) { errno = EINVAL; return -1; }
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        if (!count) return 0;
        int64_t result = pc_syscall(SYS_WRITE, (uint64_t)(uintptr_t)buffer, count, 1);
        return result < 0 ? -1 : (ssize_t)result;
    }
    if (fd == STDIN_FILENO) { errno = EBADF; return -1; }
    FILE *stream = fd_lookup(fd);
    if (!stream) return -1;
    return (ssize_t)fwrite(buffer, 1, count, stream);
}

int close(int fd) {
    if (fd < 3) return 0; // console streams stay open
    FILE *stream = fd_lookup(fd);
    if (!stream) return -1;
    fd_slots[fd] = 0;
    return fclose(stream);
}

off_t lseek(int fd, off_t offset, int origin) {
    if (fd < 3) { errno = ESPIPE; return -1; }
    FILE *stream = fd_lookup(fd);
    if (!stream) return -1;
    if (fseek(stream, (long)offset, origin) != 0) return -1;
    return (off_t)ftell(stream);
}

static void fill_stat(struct stat *info, uint64_t size, bool is_dir) {
    memset(info, 0, sizeof(*info));
    info->st_size = size;
    info->st_mode = is_dir ? S_IFDIR : S_IFREG;
}

int stat(const char *path, struct stat *info) {
    if (!path || !info) { errno = EINVAL; return -1; }
    struct file_stat_info raw;
    if (pc_file_stat(path, &raw) < 0) { errno = ENOENT; return -1; }
    fill_stat(info, raw.size, raw.is_directory != 0);
    return 0;
}

int fstat(int fd, struct stat *info) {
    if (!info) { errno = EINVAL; return -1; }
    if (fd < 3) {
        memset(info, 0, sizeof(*info));
        info->st_mode = S_IFREG;
        return 0;
    }
    FILE *stream = fd_lookup(fd);
    if (!stream) return -1;
    return stat(stdio_stream_path(stream), info);
}

int lstat(const char *path, struct stat *info) {
    return stat(path, info); // no symlinks on PureC filesystems
}

int unlink(const char *path) {
    if (!path) { errno = EINVAL; return -1; }
    if (pc_file_delete(path) < 0) { errno = ENOENT; return -1; }
    return 0;
}

int access(const char *path, int mode) {
    if (!path) { errno = EINVAL; return -1; }
    (void)mode;
    struct file_stat_info raw;
    if (pc_file_stat(path, &raw) < 0) { errno = ENOENT; return -1; }
    if ((mode & X_OK) && raw.is_directory == 0) {
        // No execute-bit model; regular files are treated as executable
        // when opened through the loader, so report success for exists.
    }
    return 0;
}

int isatty(int fd) {
    return fd >= 0 && fd < 3 ? 1 : 0;
}

unsigned int sleep(unsigned int seconds) {
    uint64_t ms = (uint64_t)seconds * 1000ULL;
    if (ms > 0xFFFFFFFFULL) ms = 0xFFFFFFFFULL;
    pc_sleep((uint32_t)ms);
    return 0;
}

char *getcwd(char *buffer, size_t capacity) {
    // No kernel cwd; the shell keeps logical location in $PWD.
    static char fallback[] = "/";
    if (!buffer || !capacity) { errno = EINVAL; return 0; }
    char value[128];
    const char *pwd = fallback;
    if (pc_getenv("PWD", value, sizeof(value)) >= 0 && value[0]) pwd = value;
    size_t n = 0;
    while (pwd[n] && n + 1 < capacity) { buffer[n] = pwd[n]; n++; }
    if (pwd[n]) { errno = ERANGE; return 0; }
    buffer[n] = '\0';
    return buffer;
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
