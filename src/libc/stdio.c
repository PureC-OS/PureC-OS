// PureC hosted libc: FILE streams over preload buffers + printf engine.
// See hosted/stdio.h for the model. Unbuffered kernel fd use: the fd is
// closed right after preload; all positioning happens in the heap buffer.

#include "include/hosted/stdio.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/errno.h"
#include "include/purec.h" // pulls in syscall.h (SYS_WRITE etc.)
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct file_impl {
    int fd;
    uint8_t *buffer;
    size_t capacity;
    size_t length;
    size_t position;
    bool readable;
    bool writable;
    bool append;
    bool eof;
    bool error;
    bool unbuffered;
    bool line_buffered;
    bool reading;
    bool writing;
    bool owns_fd;
    int pushed;
    bool has_pushed;
    int child_pid;
    char *temp_path;
};

static struct file_impl std_in_impl;
static struct file_impl std_out_impl;
static struct file_impl std_err_impl;
FILE *stdin = &std_in_impl;
FILE *stdout = &std_out_impl;
FILE *stderr = &std_err_impl;

static FILE *open_registry[FOPEN_MAX];
static uint32_t open_count = 0;
static bool std_initialized = false;
static int stdin_console_state = -1;
static unsigned temp_counter;

static void stdio_init_std(void) {
    if (std_initialized) return;
    std_initialized = true;
    memset(&std_in_impl, 0, sizeof(std_in_impl));
    memset(&std_out_impl, 0, sizeof(std_out_impl));
    memset(&std_err_impl, 0, sizeof(std_err_impl));
    std_in_impl.fd = 0;
    std_in_impl.readable = true;
    std_out_impl.fd = 1;
    std_out_impl.writable = true;
    std_out_impl.line_buffered = true;
    std_err_impl.fd = 2;
    std_err_impl.writable = true;
    std_err_impl.unbuffered = true;
}

static void registry_add(FILE *stream) {
    for (uint32_t i = 0; i < FOPEN_MAX; i++) {
        if (!open_registry[i]) { open_registry[i] = stream; return; }
    }
}

static void registry_remove(FILE *stream) {
    for (uint32_t i = 0; i < FOPEN_MAX; i++) {
        if (open_registry[i] == stream) open_registry[i] = 0;
    }
}

void stdio_stdin_reset(void) { stdin_console_state = -1; }

static bool stdin_is_console(void) {
    if (stdin_console_state < 0) {
        int64_t kind = pc_syscall(SYS_FD_CTL, 0, FD_CTL_KIND, 0);
        stdin_console_state = (kind < 0 || kind == FD_KIND_CONSOLE) ? 1 : 0;
    }
    return stdin_console_state == 1;
}

int stdio_fd_write_all(int fd, const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    size_t done = 0;
    while (done < size) {
        int64_t wrote = pc_syscall(SYS_FD_WRITE, (uint64_t)fd,
                                   (uint64_t)(uintptr_t)(bytes + done), size - done);
        if (wrote < 0) { errno = (int)-wrote; return -1; }
        if (wrote == 0) { errno = EIO; return -1; }
        done += (size_t)wrote;
    }
    return 0;
}

static bool ensure_buffer(FILE *stream) {
    if (stream->buffer) return true;
    stream->buffer = (uint8_t *)malloc(BUFSIZ);
    if (!stream->buffer) { errno = ENOMEM; stream->error = true; return false; }
    stream->capacity = BUFSIZ;
    return true;
}

static int flush_write(FILE *stream) {
    if (!stream->writing) return 0;
    int result = 0;
    if (stream->length && stdio_fd_write_all(stream->fd, stream->buffer, stream->length) < 0) {
        stream->error = true;
        result = EOF;
    }
    stream->length = 0;
    stream->writing = false;
    return result;
}

static void drop_read(FILE *stream) {
    if (!stream->reading) return;
    size_t unread = stream->length - stream->position;
    if (unread) pc_syscall(SYS_FD_SEEK, (uint64_t)stream->fd, (uint64_t)(-(int64_t)unread), SEEK_CUR);
    stream->reading = false;
    stream->length = 0;
    stream->position = 0;
}

static int fill_read(FILE *stream) {
    if (flush_write(stream) != 0) return EOF;
    if (!ensure_buffer(stream)) return EOF;
    int64_t got = pc_syscall(SYS_FD_READ, (uint64_t)stream->fd,
                             (uint64_t)(uintptr_t)stream->buffer, stream->capacity);
    if (got <= 0) {
        if (got < 0) { errno = (int)-got; stream->error = true; }
        stream->eof = true;
        return EOF;
    }
    stream->length = (size_t)got;
    stream->position = 0;
    stream->reading = true;
    return 0;
}

static FILE *stream_create(int fd, bool readable, bool writable, bool append, bool owns) {
    if (open_count >= FOPEN_MAX) { errno = EMFILE; return 0; }
    FILE *stream = (FILE *)malloc(sizeof(FILE));
    if (!stream) { errno = ENOMEM; return 0; }
    memset(stream, 0, sizeof(*stream));
    stream->fd = fd;
    stream->readable = readable;
    stream->writable = writable;
    stream->append = append;
    stream->owns_fd = owns;
    registry_add(stream);
    open_count++;
    return stream;
}

static bool parse_mode(const char *mode, int *flags, bool *read, bool *write, bool *append) {
    bool r = false, w = false, a = false, plus = false;
    for (const char *m = mode; *m; m++) {
        if (*m == 'r') r = true;
        else if (*m == 'w') w = true;
        else if (*m == 'a') a = true;
        else if (*m == '+') plus = true;
        else if (*m == 'b' || *m == 't' || *m == 'e' || *m == 'x') { }
        else return false;
    }
    if (r + w + a != 1) return false;
    *read = r || plus;
    *write = w || a || plus;
    *append = a;
    int access = plus ? 2 : (r ? 0 : 1);
    *flags = access;
    if (w) *flags |= 0100 | 01000;
    if (a) *flags |= 0100 | 02000;
    return true;
}

FILE *stdio_open_fd(int fd, const char *mode) {
    stdio_init_std();
    if (fd < 0 || !mode) { errno = EBADF; return 0; }
    int flags;
    bool read, write, append;
    if (!parse_mode(mode, &flags, &read, &write, &append)) { errno = EINVAL; return 0; }
    return stream_create(fd, read, write, append, true);
}

int stdio_stream_fd(const FILE *stream) {
    return stream ? stream->fd : -1;
}

void stdio_set_child(FILE *stream, int pid) { if (stream) stream->child_pid = pid; }
int stdio_get_child(const FILE *stream) { return stream ? stream->child_pid : 0; }

FILE *fopen(const char *path, const char *mode) {
    stdio_init_std();
    if (!path || !mode) { errno = EINVAL; return 0; }
    int flags;
    bool read, write, append;
    if (!parse_mode(mode, &flags, &read, &write, &append)) { errno = EINVAL; return 0; }
    int64_t fd = pc_syscall(SYS_FD_OPEN, (uint64_t)(uintptr_t)path, (uint64_t)(uint32_t)flags, 0);
    if (fd < 0) { errno = (int)-fd; return 0; }
    FILE *stream = stream_create((int)fd, read, write, append, true);
    if (!stream) pc_syscall(SYS_FILE_CLOSE, (uint64_t)fd, 0, 0);
    return stream;
}

int fflush(FILE *stream) {
    if (!stream) {
        stdio_init_std();
        int result = 0;
        if (fflush(stdout) != 0) result = EOF;
        if (fflush(stderr) != 0) result = EOF;
        for (uint32_t i = 0; i < FOPEN_MAX; i++) {
            if (open_registry[i] && fflush(open_registry[i]) != 0) result = EOF;
        }
        return result;
    }
    if (stream == stdin) return 0;
    return flush_write(stream);
}

int fclose(FILE *stream) {
    if (!stream || stream == stdin || stream == stdout || stream == stderr) {
        errno = EINVAL;
        return EOF;
    }
    bool known = false;
    for (uint32_t i = 0; i < FOPEN_MAX; i++) {
        if (open_registry[i] == stream) { known = true; break; }
    }
    if (!known) { errno = EBADF; return EOF; }
    int result = flush_write(stream);
    if (stream->owns_fd && pc_syscall(SYS_FILE_CLOSE, (uint64_t)stream->fd, 0, 0) < 0 && result == 0)
        result = EOF;
    if (stream->temp_path) {
        pc_file_delete(stream->temp_path);
        free(stream->temp_path);
    }
    free(stream->buffer);
    registry_remove(stream);
    if (open_count) open_count--;
    free(stream);
    return result;
}

FILE *freopen(const char *filename, const char *mode, FILE *stream) {
    stdio_init_std();
    if (!mode || !stream) { errno = EINVAL; return 0; }
    int flags;
    bool read, write, append;
    if (!parse_mode(mode, &flags, &read, &write, &append)) { errno = EINVAL; return 0; }
    flush_write(stream);
    if (!filename) {
        stream->readable = read;
        stream->writable = write;
        stream->append = append;
        return stream;
    }
    drop_read(stream);
    int64_t fd = pc_syscall(SYS_FD_OPEN, (uint64_t)(uintptr_t)filename, (uint64_t)(uint32_t)flags, 0);
    if (fd < 0) { errno = (int)-fd; return 0; }
    if (stream == stdin || stream == stdout || stream == stderr) {
        pc_syscall(SYS_DUP2, (uint64_t)fd, (uint64_t)stream->fd, 0);
        pc_syscall(SYS_FILE_CLOSE, (uint64_t)fd, 0, 0);
        if (stream == stdin) stdin_console_state = -1;
    } else {
        if (stream->owns_fd) pc_syscall(SYS_FILE_CLOSE, (uint64_t)stream->fd, 0, 0);
        stream->fd = (int)fd;
        stream->owns_fd = true;
    }
    stream->readable = read;
    stream->writable = write;
    stream->append = append;
    stream->eof = false;
    stream->error = false;
    stream->has_pushed = false;
    stream->length = stream->position = 0;
    stream->reading = stream->writing = false;
    return stream;
}

FILE *tmpfile(void) {
    stdio_init_std();
    char path[64];
    snprintf(path, sizeof(path), "/tmp/tmpf.%d.%u", (int)pc_getpid(), temp_counter++);
    int64_t fd = pc_syscall(SYS_FD_OPEN, (uint64_t)(uintptr_t)path, 02 | 0100 | 01000, 0);
    if (fd < 0) { errno = (int)-fd; return 0; }
    FILE *stream = stream_create((int)fd, true, true, false, true);
    if (!stream) { pc_syscall(SYS_FILE_CLOSE, (uint64_t)fd, 0, 0); return 0; }
    size_t length = strlen(path) + 1;
    stream->temp_path = (char *)malloc(length);
    if (stream->temp_path) memcpy(stream->temp_path, path, length);
    return stream;
}

char *tmpnam(char *s) {
    static char internal[L_tmpnam];
    char *out = s ? s : internal;
    snprintf(out, L_tmpnam, "/tmp/tmpn.%d.%u", (int)pc_getpid(), temp_counter++);
    return out;
}

int fseek(FILE *stream, long offset, int origin) {
    if (!stream) { errno = EINVAL; return -1; }
    if (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END) {
        errno = EINVAL;
        return -1;
    }
    if (flush_write(stream) != 0) return -1;
    int64_t adjust = 0;
    if (origin == SEEK_CUR) {
        if (stream->reading) adjust -= (int64_t)(stream->length - stream->position);
        if (stream->has_pushed) adjust -= 1;
    }
    int64_t result = pc_syscall(SYS_FD_SEEK, (uint64_t)stream->fd,
                                (uint64_t)((int64_t)offset + adjust), (uint64_t)origin);
    if (result < 0) { errno = ESPIPE; return -1; }
    stream->reading = false;
    stream->length = stream->position = 0;
    stream->eof = false;
    stream->has_pushed = false;
    return 0;
}

long ftell(FILE *stream) {
    if (!stream) { errno = EINVAL; return -1L; }
    int64_t position = pc_syscall(SYS_FD_SEEK, (uint64_t)stream->fd, 0, SEEK_CUR);
    if (position < 0) { errno = ESPIPE; return -1L; }
    if (stream->writing) position += (int64_t)stream->length;
    if (stream->reading) position -= (int64_t)(stream->length - stream->position);
    if (stream->has_pushed) position -= 1;
    return (long)position;
}

void rewind(FILE *stream) {
    if (!stream) return;
    fseek(stream, 0, SEEK_SET);
    stream->error = false;
}

int fgetc(FILE *stream) {
    if (!stream) { errno = EINVAL; return EOF; }
    if (stream == stdin) {
        stdio_init_std();
        if (stdin_is_console()) {
            int32_t c = (int32_t)pc_syscall(SYS_GETCHAR, 0, 0, 0);
            return c < 0 ? EOF : c & 0xFF;
        }
    }
    if (!stream->readable) { stream->error = true; errno = EBADF; return EOF; }
    if (stream->has_pushed) {
        stream->has_pushed = false;
        return stream->pushed;
    }
    if (!stream->reading || stream->position >= stream->length) {
        if (stream->reading) { stream->reading = false; stream->length = stream->position = 0; }
        if (fill_read(stream) != 0) return EOF;
    }
    return stream->buffer[stream->position++];
}

int getc(FILE *stream) { return fgetc(stream); }

int getchar(void) { return fgetc(stdin); }

char *fgets(char *buffer, int count, FILE *stream) {
    if (!buffer || count <= 0 || !stream) { errno = EINVAL; return 0; }
    int i = 0;
    while (i < count - 1) {
        int c = fgetc(stream);
        if (c == EOF) break;
        buffer[i++] = (char)c;
        if (c == '\n') break;
    }
    if (!i && feof(stream)) return 0;
    if (!i && ferror(stream)) return 0;
    buffer[i] = '\0';
    return buffer;
}

int fputc(int value, FILE *stream) {
    if (!stream) { errno = EINVAL; return EOF; }
    stdio_init_std();
    if (!stream->writable) { stream->error = true; errno = EBADF; return EOF; }
    uint8_t c = (uint8_t)value;
    if (stream->reading) {
        drop_read(stream);
        stream->has_pushed = false;
    }
    if (stream->unbuffered) {
        if (stdio_fd_write_all(stream->fd, &c, 1) < 0) { stream->error = true; return EOF; }
        return value & 0xFF;
    }
    if (!ensure_buffer(stream)) return EOF;
    if (stream->length >= stream->capacity && flush_write(stream) != 0) return EOF;
    stream->buffer[stream->length++] = c;
    stream->writing = true;
    if (stream->line_buffered && c == '\n' && flush_write(stream) != 0) return EOF;
    return value & 0xFF;
}

int putc(int value, FILE *stream) { return fputc(value, stream); }
int putchar(int value) { return fputc(value, stdout); }

int fputs(const char *text, FILE *stream) {
    if (!text || !stream) { errno = EINVAL; return EOF; }
    size_t length = strlen(text);
    return fwrite(text, 1, length, stream) == length ? 0 : EOF;
}

int puts(const char *text) {
    if (fputs(text, stdout) == EOF) return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 0;
}

int ungetc(int value, FILE *stream) {
    if (!stream || value == EOF) return EOF;
    if (!stream->readable || stream->has_pushed) return EOF;
    stream->pushed = value & 0xFF;
    stream->has_pushed = true;
    stream->eof = false;
    return stream->pushed;
}

size_t fread(void *buffer, size_t size, size_t count, FILE *stream) {
    if (!buffer || !stream) { errno = EINVAL; return 0; }
    if (size == 0 || count == 0) return 0;
    size_t total = size * count;
    uint8_t *out = (uint8_t *)buffer;
    size_t done = 0;
    if (stream == stdin) {
        stdio_init_std();
        if (stdin_is_console()) {
            for (; done < total; done++) {
                int c = fgetc(stream);
                if (c == EOF) break;
                out[done] = (uint8_t)c;
            }
            return done / size;
        }
    }
    if (!stream->readable) { stream->error = true; errno = EBADF; return 0; }
    if (stream->has_pushed && done < total) {
        stream->has_pushed = false;
        out[done++] = (uint8_t)stream->pushed;
    }
    while (done < total) {
        if (stream->reading && stream->position < stream->length) {
            size_t available = stream->length - stream->position;
            size_t take = total - done < available ? total - done : available;
            memcpy(out + done, stream->buffer + stream->position, take);
            stream->position += take;
            done += take;
            continue;
        }
        stream->reading = false;
        stream->length = stream->position = 0;
        if (flush_write(stream) != 0) break;
        if (total - done >= BUFSIZ) {
            int64_t got = pc_syscall(SYS_FD_READ, (uint64_t)stream->fd,
                                     (uint64_t)(uintptr_t)(out + done), total - done);
            if (got <= 0) {
                if (got < 0) { errno = (int)-got; stream->error = true; }
                stream->eof = true;
                break;
            }
            done += (size_t)got;
            continue;
        }
        if (fill_read(stream) != 0) break;
    }
    return done / size;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream) {
    if (!buffer || !stream) { errno = EINVAL; return 0; }
    if (size == 0 || count == 0) return 0;
    stdio_init_std();
    if (!stream->writable) { stream->error = true; errno = EBADF; return 0; }
    size_t total = size * count;
    const uint8_t *in = (const uint8_t *)buffer;
    if (stream->reading) {
        drop_read(stream);
        stream->has_pushed = false;
    }
    if (stream->unbuffered || total >= BUFSIZ) {
        if (flush_write(stream) != 0) return 0;
        if (stdio_fd_write_all(stream->fd, in, total) < 0) { stream->error = true; return 0; }
        return count;
    }
    if (!ensure_buffer(stream)) return 0;
    size_t done = 0;
    while (done < total) {
        if (stream->length >= stream->capacity && flush_write(stream) != 0) return done / size;
        size_t room = stream->capacity - stream->length;
        size_t take = total - done < room ? total - done : room;
        memcpy(stream->buffer + stream->length, in + done, take);
        stream->length += take;
        stream->writing = true;
        done += take;
    }
    if (stream->line_buffered && memchr(in, '\n', total) && flush_write(stream) != 0)
        return 0;
    return count;
}

int feof(FILE *stream) { return stream ? (stream->eof ? 1 : 0) : 1; }
int ferror(FILE *stream) { return stream ? (stream->error ? 1 : 0) : 1; }

void clearerr(FILE *stream) {
    if (!stream) return;
    stream->eof = false;
    stream->error = false;
}

void perror(const char *prefix) {
    if (prefix && *prefix) {
        fputs(prefix, stderr);
        fputs(": ", stderr);
    }
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
}

int remove(const char *path) {    if (!path) { errno = EINVAL; return -1; }
    char abs[512];
    if (path[0] != '/') {
        if (realpath(path, abs)) path = abs;
    }
    if (pc_file_delete(path) < 0) { errno = ENOENT; return -1; }
    return 0;
}

int rename(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) { errno = EINVAL; return -1; }
    char abs_old[512];
    char abs_new[512];
    if (old_path[0] != '/') {
        if (realpath(old_path, abs_old)) old_path = abs_old;
    }
    if (new_path[0] != '/') {
        if (realpath(new_path, abs_new)) new_path = abs_new;
    }
    // pc_file_rename takes (path, new_name); extract the basename.
    const char *base = new_path;
    for (const char *p = new_path; *p; p++) {
        if (*p == '/') base = p + 1;
    }
    if (pc_file_rename(old_path, base) < 0) { errno = ENOENT; return -1; }
    return 0;
}

int setvbuf(FILE *stream, char *buffer, int mode, size_t size) {
    (void)buffer; (void)size;
    if (!stream) { errno = EINVAL; return -1; }
    stream->unbuffered = mode == _IONBF;
    stream->line_buffered = mode == _IOLBF;
    return 0;
}

void setbuf(FILE *stream, char *buffer) {
    if (stream) setvbuf(stream, buffer, buffer ? _IOFBF : _IONBF, BUFSIZ);
}

// ---- printf engine (integers, chars, strings, pointers; no floats) ----

struct fmt_sink {
    char *buffer;
    size_t capacity; // 0 = unbounded (sprintfLocker: grows via malloc)
    size_t length;
    FILE *stream;
    uint8_t *dynamic; // for sprintf growth
    size_t dyn_cap;
    bool error;
};

static void sink_putc(struct fmt_sink *sink, char c) {
    if (sink->stream) {
        if (fputc((unsigned char)c, sink->stream) == EOF) sink->error = true;
        else sink->length++;
        return;
    }
    if (sink->dynamic) {
        if (sink->length + 1 >= sink->dyn_cap) {
            size_t grown = sink->dyn_cap * 2;
            uint8_t *fresh = (uint8_t *)realloc(sink->dynamic, grown);
            if (!fresh) { sink->error = true; return; }
            sink->dynamic = fresh;
            sink->buffer = (char *)fresh;
            sink->capacity = grown;
            sink->dyn_cap = grown;
        }
        sink->buffer[sink->length++] = c;
        return;
    }
    // Bounded (snprintf/vsprintf-target): length always counts what WOULD
    // have been written (C99); only in-range bytes are stored.
    if (sink->buffer && sink->capacity && sink->length + 1 < sink->capacity) {
        sink->buffer[sink->length] = c;
    }
    sink->length++;
}

static void sink_write(const struct fmt_sink *outer, const char *text, size_t count) {
    struct fmt_sink *sink = (struct fmt_sink *)outer;
    for (size_t i = 0; i < count; i++) sink_putc(sink, text[i]);
}

static void format_unsigned(struct fmt_sink *sink, unsigned long long value,
                            int base, bool upper, int width, int precision,
                            bool left, bool zero, bool alternate) {
    char digits[64];
    int count = 0;
    const char *alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (value == 0) {
        if (precision != 0) digits[count++] = '0';
    } else {
        while (value) { digits[count++] = alphabet[value % (unsigned)base]; value /= (unsigned)base; }
    }
    const char *prefix = "";
    int prefix_len = 0;
    if (alternate && base == 16 && count) { prefix = upper ? "0X" : "0x"; prefix_len = 2; }
    else if (alternate && base == 8 && (count == 0 || digits[count - 1] != '0')) {
        prefix = "0"; prefix_len = 1;
    }
    while (count < precision) digits[count++] = '0';
    int total = prefix_len + count;
    int pad = width > total ? width - total : 0;
    char pad_char = (zero && !left && precision < 0) ? '0' : ' ';
    if (!left) {
        if (pad_char == '0') {
            sink_write(sink, prefix, (size_t)prefix_len);
            for (int i = 0; i < pad; i++) sink_putc(sink, '0');
        } else {
            for (int i = 0; i < pad; i++) sink_putc(sink, ' ');
            sink_write(sink, prefix, (size_t)prefix_len);
        }
    } else {
        sink_write(sink, prefix, (size_t)prefix_len);
    }
    for (int i = count - 1; i >= 0; i--) sink_putc(sink, digits[i]);
    if (left) {
        for (int i = 0; i < pad; i++) sink_putc(sink, ' ');
    }
}

static const unsigned long long printf_pow10[19] = {
    1ULL, 10ULL, 100ULL, 1000ULL, 10000ULL, 100000ULL, 1000000ULL,
    10000000ULL, 100000000ULL, 1000000000ULL, 10000000000ULL,
    100000000000ULL, 1000000000000ULL, 10000000000000ULL,
    100000000000000ULL, 1000000000000000ULL, 10000000000000000ULL,
    100000000000000000ULL, 1000000000000000000ULL
};

// sign must already be emitted by the caller when zero==false; when
// zero==true the sign goes first, then zero padding (mirrors integers).
static void format_double(struct fmt_sink *sink, double value, int precision,
                          int width, bool left, bool zero, bool plus,
                          bool space, char spec) {
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    bool negative = (bits >> 63) != 0;
    bool scientific = spec == 'e' || spec == 'E';
    char exp_mark = spec == 'E' ? 'E' : 'e';
    char sign = 0;
    if (negative) sign = '-';
    else if (plus) sign = '+';
    else if (space) sign = ' ';
    if (((bits >> 52) & 0x7FF) == 0x7FF) {
        bool is_nan = (bits & 0xFFFFFFFFFFFFFULL) != 0;
        bool upper = spec == 'F' || spec == 'E';
        const char *text = is_nan ? (upper ? "NAN" : "nan")
                                  : (upper ? "INF" : "inf");
        if (is_nan && !negative) sign = 0;
        int count = 3;
        int pad = width > count + (sign ? 1 : 0) ? width - count - (sign ? 1 : 0) : 0;
        if (!left) { for (int i = 0; i < pad; i++) sink_putc(sink, ' '); }
        if (sign) sink_putc(sink, sign);
        sink_write(sink, text, 3);
        if (left) { for (int i = 0; i < pad; i++) sink_putc(sink, ' '); }
        return;
    }
    if (precision < 0) precision = 6;
    if (precision > 18) precision = 18;
    double magnitude = negative ? -value : value;
    // Fixed notation overflows u64 past 2^63; delegate those to %e.
    if (!scientific && magnitude >= 9.223372036854776e18) scientific = true;
    int decimal_exp = 0;
    unsigned long long int_part = 0;
    unsigned long long frac_part = 0;
    if (scientific) {
        if (magnitude == 0.0) {
            int_part = 0;
            frac_part = 0;
            decimal_exp = 0;
        } else {
            while (magnitude >= 10.0) { magnitude /= 10.0; decimal_exp++; }
            while (magnitude < 1.0) { magnitude *= 10.0; decimal_exp--; }
            unsigned long long scaled =
                (unsigned long long)(magnitude * (double)printf_pow10[precision] + 0.5);
            if (scaled >= printf_pow10[precision] * 10ULL) {
                scaled /= 10ULL;
                decimal_exp++;
            }
            int_part = scaled / printf_pow10[precision];
            frac_part = scaled % printf_pow10[precision];
        }
    } else {
        int_part = (unsigned long long)magnitude;
        double frac = magnitude - (double)int_part;
        frac_part = (unsigned long long)(frac * (double)printf_pow10[precision] + 0.5);
        if (frac_part >= printf_pow10[precision]) {
            frac_part = 0;
            int_part++;
        }
    }
    // Digit count of the integer part.
    char reversed[24];
    int int_digits = 0;
    if (int_part == 0) reversed[int_digits++] = '0';
    else {
        unsigned long long tmp = int_part;
        while (tmp) { reversed[int_digits++] = (char)('0' + tmp % 10); tmp /= 10; }
    }
    int total = int_digits + (sign ? 1 : 0);
    // Plain %f omits the point at precision 0.
    int point = precision > 0 ? 1 : 0;
    total += point + precision;
    int exp_len = 0;
    if (scientific) {
        int e = decimal_exp < 0 ? -decimal_exp : decimal_exp;
        exp_len = 2 + 1; // 'e', sign, at least 2 digits
        if (e >= 100) exp_len++;
    }
    total += exp_len;
    int pad = width > total ? width - total : 0;
    char pad_char = (zero && !left) ? '0' : ' ';
    if (!left) {
        if (pad_char == '0') {
            if (sign) sink_putc(sink, sign);
            for (int i = 0; i < pad; i++) sink_putc(sink, '0');
        } else {
            for (int i = 0; i < pad; i++) sink_putc(sink, ' ');
            if (sign) sink_putc(sink, sign);
        }
    } else {
        if (sign) sink_putc(sink, sign);
    }
    for (int i = int_digits - 1; i >= 0; i--) sink_putc(sink, reversed[i]);
    if (point) {
        sink_putc(sink, '.');
        unsigned long long divisor = printf_pow10[precision ? precision - 1 : 0];
        for (int i = 0; i < precision; i++) {
            sink_putc(sink, (char)('0' + (frac_part / divisor) % 10));
            divisor /= 10;
        }
    }
    if (scientific) {
        int e = decimal_exp < 0 ? -decimal_exp : decimal_exp;
        sink_putc(sink, exp_mark);
        sink_putc(sink, decimal_exp < 0 ? '-' : '+');
        if (e >= 100) sink_putc(sink, (char)('0' + e / 100));
        sink_putc(sink, (char)('0' + (e / 10) % 10));
        sink_putc(sink, (char)('0' + e % 10));
    }
    if (left) {
        for (int i = 0; i < pad; i++) sink_putc(sink, ' ');
    }
}

static void format_signed(struct fmt_sink *sink, long long value, int width,
                          int precision, bool left, bool zero, bool plus, bool space) {
    bool negative = value < 0;
    unsigned long long magnitude = negative ? (unsigned long long)(-(value + 1)) + 1ULL : (unsigned long long)value;
    char digits[64];
    int count = 0;
    if (magnitude == 0) {
        if (precision != 0) digits[count++] = '0';
    } else {
        while (magnitude) { digits[count++] = (char)('0' + magnitude % 10); magnitude /= 10; }
    }
    while (count < precision) digits[count++] = '0';
    char sign = 0;
    if (negative) sign = '-';
    else if (plus) sign = '+';
    else if (space) sign = ' ';
    int total = count + (sign ? 1 : 0);
    int pad = width > total ? width - total : 0;
    char pad_char = (zero && !left && precision < 0) ? '0' : ' ';
    if (!left) {
        if (pad_char == '0') {
            if (sign) sink_putc(sink, sign);
            for (int i = 0; i < pad; i++) sink_putc(sink, '0');
        } else {
            for (int i = 0; i < pad; i++) sink_putc(sink, ' ');
            if (sign) sink_putc(sink, sign);
        }
    } else {
        if (sign) sink_putc(sink, sign);
    }
    for (int i = count - 1; i >= 0; i--) sink_putc(sink, digits[i]);
    if (left) {
        for (int i = 0; i < pad; i++) sink_putc(sink, ' ');
    }
}

static int format_core(struct fmt_sink *sink, const char *format, va_list args) {
    size_t start = sink->length;
    for (; *format; format++) {
        if (*format != '%') { sink_putc(sink, *format); continue; }
        format++;
        bool left = false, plus = false, space = false, alternate = false, zero = false;
        bool parsing = true;
        while (parsing) {
            switch (*format) {
                case '-': left = true; format++; break;
                case '+': plus = true; format++; break;
                case ' ': space = true; format++; break;
                case '#': alternate = true; format++; break;
                case '0': zero = true; format++; break;
                default: parsing = false; break;
            }
        }
        int width = 0;
        bool width_star = false;
        if (*format == '*') { width_star = true; format++; }
        else { while (*format >= '0' && *format <= '9') { width = width * 10 + (*format - '0'); format++; } }
        int precision = -1;
        if (*format == '.') {
            format++;
            if (*format == '*') { format++; precision = va_arg(args, int); }
            else {
                precision = 0;
                while (*format >= '0' && *format <= '9') { precision = precision * 10 + (*format - '0'); format++; }
            }
        }
        int length = 0; // 0=none/int, 1=h, 2=hh, 3=l, 4=ll, 5=j, 6=z, 7=t
        if (*format == 'h') { format++; if (*format == 'h') { length = 2; format++; } else length = 1; }
        else if (*format == 'l') { format++; if (*format == 'l') { length = 4; format++; } else length = 3; }
        else if (*format == 'j') { length = 5; format++; }
        else if (*format == 'z') { length = 6; format++; }
        else if (*format == 't') { length = 7; format++; }
        if (width_star) {
            width = va_arg(args, int);
            if (width < 0) { width = -width; left = true; }
        }
        if (precision < 0 && precision != -1) precision = -1;
        char spec = *format;
        if (!spec) break;
        switch (spec) {
            case 'd':
            case 'i': {
                long long value;
                if (length == 4 || length == 5) value = va_arg(args, long long);
                else if (length == 3 || length == 6 || length == 7) value = (long long)va_arg(args, long);
                else value = (long long)va_arg(args, int);
                format_signed(sink, value, width, precision, left, zero, plus, space);
                break;
            }
            case 'u':
            case 'o':
            case 'x':
            case 'X': {
                unsigned long long value;
                if (length == 4 || length == 5) value = va_arg(args, unsigned long long);
                else if (length == 3 || length == 6 || length == 7) value = (unsigned long long)va_arg(args, unsigned long);
                else value = (unsigned long long)va_arg(args, unsigned int);
                int base = spec == 'o' ? 8 : (spec == 'u' ? 10 : 16);
                format_unsigned(sink, value, base, spec == 'X', width, precision, left, zero, alternate);
                break;
            }
            case 'c': {
                int c = va_arg(args, int);
                int pad = width > 1 ? width - 1 : 0;
                if (!left) { for (int i = 0; i < pad; i++) sink_putc(sink, ' '); }
                sink_putc(sink, (char)c);
                if (left) { for (int i = 0; i < pad; i++) sink_putc(sink, ' '); }
                break;
            }
            case 's': {
                const char *text = va_arg(args, const char *);
                if (!text) text = "(null)";
                size_t count = 0;
                while (text[count] && (precision < 0 || count < (size_t)precision)) count++;
                int pad = width > (int)count ? width - (int)count : 0;
                if (!left) { for (int i = 0; i < pad; i++) sink_putc(sink, ' '); }
                sink_write(sink, text, count);
                if (left) { for (int i = 0; i < pad; i++) sink_putc(sink, ' '); }
                break;
            }
            case 'p': {
                void *pointer = va_arg(args, void *);
                unsigned long long value = (unsigned long long)(uintptr_t)pointer;
                // %p ignores flags except width/left, always 0x-prefixed.
                char digits[32];
                int count = 0;
                if (!value) digits[count++] = '0';
                while (value) { digits[count++] = "0123456789abcdef"[value & 0xF]; value >>= 4; }
                int total = count + 2;
                int pad = width > total ? width - total : 0;
                if (!left) { for (int i = 0; i < pad; i++) sink_putc(sink, ' '); }
                sink_putc(sink, '0'); sink_putc(sink, 'x');
                for (int i = count - 1; i >= 0; i--) sink_putc(sink, digits[i]);
                if (left) { for (int i = 0; i < pad; i++) sink_putc(sink, ' '); }
                break;
            }
            case 'n': {
                void *pointer = va_arg(args, void *);
                size_t done = sink->length - start;
                if (length == 2) *(signed char *)pointer = (signed char)done;
                else if (length == 1) *(short *)pointer = (short)done;
                else if (length == 3) *(long *)pointer = (long)done;
                else if (length == 4) *(long long *)pointer = (long long)done;
                else *(int *)pointer = (int)done;
                break;
            }
            case '%':
                sink_putc(sink, '%');
                break;
            case 'f':
            case 'F':
            case 'e':
            case 'E': {
                double value = va_arg(args, double);
                format_double(sink, value, precision, width, left, zero,
                              plus, space, spec);
                break;
            }
            default:
                // Unknown (incl. %g/%G/%a without FP-string support):
                // print literally so output stays aligned and debuggable.
                sink_putc(sink, '%');
                sink_putc(sink, spec);
                break;
        }
    }
    if (sink->error) return -1;
    return (int)(sink->length - start);
}

int vfprintf(FILE *stream, const char *format, va_list args) {
    if (!stream || !format) { errno = EINVAL; return -1; }
    struct fmt_sink sink;
    memset(&sink, 0, sizeof(sink));
    sink.stream = stream;
    return format_core(&sink, format, args);
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vfprintf(stream, format, args);
    va_end(args);
    return result;
}

int vprintf(const char *format, va_list args) {
    return vfprintf(stdout, format, args);
}

size_t stdio_stream_peek(FILE *stream, char *out, size_t capacity) {
    stdio_init_std();
    if (!stream || !stream->readable) return 0;
    if (stream->has_pushed) return 0;
    if (stream == stdin && stdin_is_console()) return 0;
    if (!stream->reading || stream->position >= stream->length) {
        stream->reading = false;
        stream->length = stream->position = 0;
        if (fill_read(stream) != 0) return 0;
    }
    size_t available = stream->length - stream->position;
    if (available > capacity) available = capacity;
    memcpy(out, stream->buffer + stream->position, available);
    return available;
}

void stdio_stream_advance(FILE *stream, size_t count) {
    if (!stream) return;
    stream->position += count;
    if (stream->position > stream->length) stream->position = stream->length;
}

int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vfprintf(stdout, format, args);
    va_end(args);
    return result;
}

int vsnprintf(char *buffer, size_t capacity, const char *format, va_list args) {
    if (!format) { errno = EINVAL; return -1; }
    if (!buffer && capacity) { errno = EINVAL; return -1; }
    struct fmt_sink sink;
    memset(&sink, 0, sizeof(sink));
    sink.buffer = buffer;
    sink.capacity = capacity;
    int result = format_core(&sink, format, args);
    if (buffer && capacity) {
        buffer[sink.length < capacity ? sink.length : capacity - 1] = '\0';
    }
    return result;
}

int snprintf(char *buffer, size_t capacity, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsnprintf(buffer, capacity, format, args);
    va_end(args);
    return result;
}

int sprintf(char *buffer, const char *format, ...) {
    // Unbounded by nature (same contract as the original); used only
    // where callers size the buffer. Grows via heap internally.
    struct fmt_sink sink;
    memset(&sink, 0, sizeof(sink));
    sink.dynamic = (uint8_t *)malloc(256);
    if (!sink.dynamic) return -1;
    sink.buffer = (char *)sink.dynamic;
    sink.capacity = 256;
    sink.dyn_cap = 256;
    va_list args;
    va_start(args, format);
    int result = format_core(&sink, format, args);
    va_end(args);
    if (result < 0 || sink.error) { free(sink.dynamic); return -1; }
    sink_putc(&sink, '\0');
    memcpy(buffer, sink.buffer, (size_t)result + 1);
    free(sink.dynamic);
    return result;
}
