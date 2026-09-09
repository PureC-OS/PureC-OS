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
    int fd; // kept only while preloading; -1 afterwards (except stdio)
    uint8_t *buffer;
    size_t size;
    size_t capacity;
    size_t position;
    char path[FILENAME_MAX];
    bool readable;
    bool writable;
    bool append;
    bool eof;
    bool error;
    bool unbuffered;
    bool line_buffered;
    int pushed;
    bool has_pushed;
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

// ---- open/close ----

FILE *fopen(const char *path, const char *mode) {
    stdio_init_std();
    if (!path || !mode) { errno = EINVAL; return 0; }
    bool read = false, write = false, append = false, truncate = false, must_exist = false;
    // Parse "r/w/a" + "+" + "b"/"t".
    for (const char *m = mode; *m; m++) {
        if (*m == 'r') { read = true; must_exist = true; }
        else if (*m == 'w') { write = true; truncate = true; }
        else if (*m == 'a') { write = true; append = true; }
        else if (*m == '+') { read = true; write = true; must_exist = !truncate; }
        else if (*m == 'b' || *m == 't') { }
        else { errno = EINVAL; return 0; }
    }
    if (!read && !write) { errno = EINVAL; return 0; }
    if (open_count >= FOPEN_MAX) { errno = EMFILE; return 0; }

    FILE *stream = (FILE *)malloc(sizeof(FILE));
    if (!stream) return 0;
    memset(stream, 0, sizeof(*stream));
    stream->fd = -1;
    strncpy(stream->path, path, sizeof(stream->path) - 1);
    stream->readable = read;
    stream->writable = write;
    stream->append = append;

    if (truncate) {
        // Create-or-replace with empty content (full-replace write model).
        static char empty = 0;
        int32_t probe = pc_file_open(path);
        if (probe >= 0) {
            pc_file_close(probe);
            if (pc_file_write(path, &empty, 0) < 0) {
                free(stream);
                errno = EIO;
                return 0;
            }
        } else {
            if (pc_file_create(path) < 0) {
                free(stream);
                errno = ENOENT;
                return 0;
            }
        }
    }

    if (read || append) {
        // Preload whole file into the heap buffer.
        struct file_stat_info info;
        uint64_t file_size = 0;
        if (pc_file_stat(path, &info) == 0) file_size = info.size;
        if (must_exist && file_size == 0) {
            // Distinguish missing file from empty file via open probe.
            int32_t probe = pc_file_open(path);
            if (probe < 0) {
                free(stream);
                errno = ENOENT;
                return 0;
            }
            pc_file_close(probe);
        }
        if (file_size > 0) {
            if (file_size > 64 * 1024 * 1024) {
                free(stream);
                errno = ENOMEM;
                return 0;
            }
            stream->buffer = (uint8_t *)malloc((size_t)file_size);
            if (!stream->buffer) { free(stream); return 0; }
            int32_t fd = pc_file_open(path);
            if (fd < 0) { free(stream->buffer); free(stream); errno = ENOENT; return 0; }
            uint64_t done = 0;
            while (done < file_size) {
                uint32_t chunk = file_size - done > 0x40000 ? 0x40000 : (uint32_t)(file_size - done);
                int32_t got = pc_file_read(fd, stream->buffer + done, chunk);
                if (got <= 0) break;
                done += (uint64_t)got;
            }
            pc_file_close(fd);
            stream->size = (size_t)done;
            stream->capacity = (size_t)file_size;
        }
        stream->position = append ? stream->size : 0;
    }
    registry_add(stream);
    open_count++;
    return stream;
}

int fflush(FILE *stream) {
    if (!stream) {
        int result = 0;
        // Console streams are not in the registry; flush them explicitly.
        if (fflush(stdout) != 0) result = EOF;
        if (fflush(stderr) != 0) result = EOF;
        for (uint32_t i = 0; i < FOPEN_MAX; i++) {
            if (open_registry[i] && fflush(open_registry[i]) != 0) result = EOF;
        }
        return result;
    }
    if (stream == stdout || stream == stderr || stream == stdin) {
        // Console streams flush inline on write; drain stdout's line buffer.
        if (stream == stdout && stream->buffer && stream->size) {
            pc_syscall(SYS_WRITE, (uint64_t)(uintptr_t)stream->buffer, stream->size, 1);
            stream->size = 0;
        }
        return 0;
    }
    if (!stream->writable || !stream->buffer || !stream->size) return 0;
    if (pc_file_write(stream->path, stream->buffer, (uint32_t)stream->size) < 0) {
        stream->error = true;
        errno = EIO;
        return EOF;
    }
    return 0;
}

int fclose(FILE *stream) {
    if (!stream || stream == stdin || stream == stdout || stream == stderr) {
        errno = EINVAL;
        return EOF;
    }
    int result = fflush(stream);
    if (stream->fd >= 0) pc_file_close(stream->fd);
    free(stream->buffer);
    registry_remove(stream);
    if (open_count) open_count--;
    free(stream);
    return result;
}

// ---- positioning ----

int fseek(FILE *stream, long offset, int origin) {
    if (!stream) { errno = EINVAL; return -1; }
    if (stream == stdin) { errno = ESPIPE; return -1; }
    if (stream == stdout || stream == stderr) { errno = ESPIPE; return -1; }
    if (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END) {
        errno = EINVAL;
        return -1;
    }
    int64_t base = 0;
    if (origin == SEEK_CUR) base = (int64_t)stream->position;
    else if (origin == SEEK_END) base = (int64_t)stream->size;
    int64_t target = base + offset;
    if (target < 0) { errno = EINVAL; return -1; }
    stream->position = (size_t)target;
    stream->eof = false;
    stream->has_pushed = false;
    return 0;
}

long ftell(FILE *stream) {
    if (!stream) { errno = EINVAL; return -1L; }
    if (stream == stdin || stream == stdout || stream == stderr) return 0L;
    return (long)stream->position;
}

void rewind(FILE *stream) {
    if (!stream) return;
    if (stream == stdin || stream == stdout || stream == stderr) return;
    stream->position = 0;
    stream->eof = false;
    stream->error = false;
    stream->has_pushed = false;
}

// ---- character/block IO ----

static int file_getc_buffered(FILE *stream) {
    if (stream->has_pushed) {
        stream->has_pushed = false;
        return stream->pushed;
    }
    if (stream->position >= stream->size) {
        stream->eof = true;
        return EOF;
    }
    return stream->buffer[stream->position++];
}

int fgetc(FILE *stream) {
    if (!stream) { errno = EINVAL; return EOF; }
    if (stream == stdin) {
        int32_t c = (int32_t)pc_syscall(SYS_GETCHAR, 0, 0, 0); // blocking
        return c < 0 ? EOF : c & 0xFF;
    }
    if (!stream->readable) { stream->error = true; errno = EBADF; return EOF; }
    return file_getc_buffered(stream);
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
    buffer[i] = '\0';
    return buffer;
}

static bool file_ensure_capacity(FILE *stream, size_t extra) {
    if (stream->position + extra <= stream->capacity) return true;
    size_t need = stream->position + extra;
    size_t grown = stream->capacity ? stream->capacity * 2 : 256;
    while (grown < need) grown *= 2;
    uint8_t *fresh = (uint8_t *)realloc(stream->buffer, grown);
    if (!fresh) { stream->error = true; errno = ENOMEM; return false; }
    stream->buffer = fresh;
    stream->capacity = grown;
    return true;
}

int fputc(int value, FILE *stream) {
    if (!stream) { errno = EINVAL; return EOF; }
    if (stream == stdout || stream == stderr) {
        uint8_t c = (uint8_t)value;
        if (stream == stderr) {
            pc_syscall(1, (uint64_t)(uintptr_t)&c, 1, 1);
            return value & 0xFF;
        }
        // stdout: line buffered.
        if (!stream->buffer) {
            stream->buffer = (uint8_t *)malloc(BUFSIZ);
            if (!stream->buffer) { errno = ENOMEM; return EOF; }
            stream->capacity = BUFSIZ;
            stream->size = 0;
        }
        if (!file_ensure_capacity(stream, 1)) return EOF;
        stream->buffer[stream->size++] = c;
        if (c == '\n') fflush(stream);
        return value & 0xFF;
    }
    if (!stream->writable) { stream->error = true; errno = EBADF; return EOF; }
    if (stream->append) stream->position = stream->size;
    if (!file_ensure_capacity(stream, 1)) return EOF;
    if (stream->position >= stream->size) stream->size = stream->position + 1;
    stream->buffer[stream->position++] = (uint8_t)value;
    return value & 0xFF;
}

int putc(int value, FILE *stream) { return fputc(value, stream); }
int putchar(int value) { return fputc(value, stdout); }

int fputs(const char *text, FILE *stream) {
    if (!text || !stream) { errno = EINVAL; return EOF; }
    for (; *text; text++) {
        if (fputc((unsigned char)*text, stream) == EOF) return EOF;
    }
    return 0;
}

int puts(const char *text) {
    if (fputs(text, stdout) == EOF) return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 0;
}

int ungetc(int value, FILE *stream) {
    if (!stream || value == EOF) return EOF;
    if (stream == stdin || stream == stdout || stream == stderr) {
        errno = ESPIPE;
        return EOF;
    }
    if (!stream->readable || stream->has_pushed) return EOF;
    stream->pushed = value & 0xFF;
    stream->has_pushed = true;
    stream->eof = false;
    return stream->pushed;
}

size_t fread(void *buffer, size_t size, size_t count, FILE *stream) {
    if (!buffer || !stream) { errno = EINVAL; return 0; }
    if (size == 0 || count == 0) return 0;
    if (stream == stdin) {
        uint8_t *out = (uint8_t *)buffer;
        size_t got = 0;
        for (size_t i = 0; i < count; i++) {
            for (size_t k = 0; k < size; k++) {
                int c = fgetc(stream);
                if (c == EOF) return got;
                out[i * size + k] = (uint8_t)c;
            }
            got++;
        }
        return got;
    }
    if (!stream->readable) { stream->error = true; errno = EBADF; return 0; }
    size_t done = 0;
    uint8_t *out = (uint8_t *)buffer;
    for (size_t i = 0; i < count; i++) {
        for (size_t k = 0; k < size; k++) {
            int c = file_getc_buffered(stream);
            if (c == EOF) return done;
            out[i * size + k] = (uint8_t)c;
        }
        done++;
    }
    return done;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream) {
    if (!buffer || !stream) { errno = EINVAL; return 0; }
    if (size == 0 || count == 0) return 0;
    const uint8_t *in = (const uint8_t *)buffer;
    for (size_t i = 0; i < count; i++) {
        for (size_t k = 0; k < size; k++) {
            if (fputc(in[i * size + k], stream) == EOF) return i;
        }
    }
    return count;
}

int feof(FILE *stream) { return stream ? (stream->eof ? 1 : 0) : 1; }
int ferror(FILE *stream) { return stream ? (stream->error ? 1 : 0) : 1; }

void clearerr(FILE *stream) {
    if (!stream) return;
    stream->eof = false;
    stream->error = false;
    stream->has_pushed = false;
}

void perror(const char *prefix) {
    if (prefix && *prefix) {
        fputs(prefix, stderr);
        fputs(": ", stderr);
    }
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
}

int remove(const char *path) {
    if (!path) { errno = EINVAL; return -1; }
    if (pc_file_delete(path) < 0) { errno = ENOENT; return -1; }
    return 0;
}

int rename(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) { errno = EINVAL; return -1; }
    // pc_file_rename takes (path, new_name); extract the basename.
    const char *base = new_path;
    for (const char *p = new_path; *p; p++) {
        if (*p == '/') base = p + 1;
    }
    if (pc_file_rename(old_path, base) < 0) { errno = ENOENT; return -1; }
    return 0;
}

int setvbuf(FILE *stream, char *buffer, int mode, size_t size) {
    (void)stream; (void)buffer; (void)mode; (void)size;
    return 0; // buffering model is fixed; accepted for compatibility
}

void setbuf(FILE *stream, char *buffer) {
    (void)stream; (void)buffer;
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
            default:
                // Unknown (incl. float %f/%e/%g without SSE/FPU support):
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
