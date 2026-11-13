#include "include/hosted/errno.h"
#include "include/hosted/fcntl.h"
#include "include/hosted/getopt.h"
#include "include/hosted/langinfo.h"
#include "include/hosted/libgen.h"
#include "include/hosted/fnmatch.h"
#include "include/hosted/pthread.h"
#include "include/hosted/stdio.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/time.h"
#include "include/hosted/unistd.h"
#include "include/purec.h"
#include "include/hosted/wchar.h"
#include "include/purec.h"
#include <stddef.h>
#include <stdint.h>

char *optarg;
int optind = 1;
int opterr = 1;
int optopt;
static int option_cursor;

static int option_error(const char *program, const char *message, int c)
{
    if (opterr)
        fprintf(stderr, "%s: %s -- '%c'\n", program, message, c);
    optopt = c;
    return '?';
}

int getopt(int argc, char *const argv[], const char *optstring)
{
    int quiet = optstring[0] == ':';
    if (quiet)
        optstring++;
    optarg = 0;
    if (optind >= argc || !argv[optind])
        return -1;
    const char *argument = argv[optind];
    if (!option_cursor)
    {
        if (argument[0] != '-' || !argument[1])
            return -1;
        if (argument[1] == '-' && !argument[2])
        {
            optind++;
            return -1;
        }
        option_cursor = 1;
    }
    int c = argument[option_cursor++];
    const char *found = strchr(optstring, c);
    int last = !argument[option_cursor];
    if (c == ':' || !found)
    {
        if (last)
        {
            optind++;
            option_cursor = 0;
        }
        if (quiet)
        {
            optopt = c;
            return '?';
        }
        return option_error(argv[0], "invalid option", c);
    }
    if (found[1] == ':')
    {
        if (!last)
        {
            optarg = (char *)&argument[option_cursor];
            optind++;
        }
        else if (found[2] == ':')
            optind++;
        else if (optind + 1 < argc)
        {
            optarg = argv[optind + 1];
            optind += 2;
        }
        else
        {
            optind++;
            option_cursor = 0;
            if (quiet)
            {
                optopt = c;
                return ':';
            }
            return option_error(argv[0], "option requires an argument", c);
        }
        option_cursor = 0;
    }
    else if (last)
    {
        optind++;
        option_cursor = 0;
    }
    return c;
}

static int long_option(int argc, char *const argv[], const char *optstring,
                       const struct option *longopts, int *longindex,
                       int only)
{
    const char *argument = argv[optind];
    if (argument[0] == '-' && argument[1] == '-' && argument[2])
        argument += 2;
    else if (only && argument[0] == '-' && argument[1])
        argument += 1;
    else
        return getopt(argc, argv, optstring);
    const char *equals = strchr(argument, '=');
    size_t length = equals ? (size_t)(equals - argument) : strlen(argument);
    int match = -1;
    for (int i = 0; longopts[i].name; i++)
    {
        if (strncmp(longopts[i].name, argument, length) == 0)
        {
            if (strlen(longopts[i].name) == length)
            {
                match = i;
                break;
            }
            if (match >= 0)
            {
                if (opterr)
                    fprintf(stderr, "%s: option '%s' is ambiguous\n", argv[0], argv[optind]);
                optind++;
                return '?';
            }
            match = i;
        }
    }
    if (match < 0)
    {
        if (only)
            return getopt(argc, argv, optstring);
        if (opterr)
            fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], argv[optind]);
        optind++;
        return '?';
    }
    optind++;
    optarg = 0;
    const struct option *chosen = &longopts[match];
    if (chosen->has_arg == no_argument && equals)
    {
        if (opterr)
            fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n", argv[0], chosen->name);
        return '?';
    }
    if (chosen->has_arg != no_argument)
    {
        if (equals)
            optarg = (char *)equals + 1;
        else if (chosen->has_arg == required_argument)
        {
            if (optind >= argc)
            {
                if (opterr)
                    fprintf(stderr, "%s: option '--%s' requires an argument\n", argv[0], chosen->name);
                return optstring[0] == ':' ? ':' : '?';
            }
            optarg = argv[optind++];
        }
    }
    if (longindex)
        *longindex = match;
    if (chosen->flag)
    {
        *chosen->flag = chosen->val;
        return 0;
    }
    return chosen->val;
}

int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex)
{
    if (optind >= argc || !argv[optind])
        return -1;
    if (option_cursor)
        return getopt(argc, argv, optstring);
    return long_option(argc, argv, optstring, longopts, longindex, 0);
}

int getopt_long_only(int argc, char *const argv[], const char *optstring,
                     const struct option *longopts, int *longindex)
{
    if (optind >= argc || !argv[optind])
        return -1;
    if (option_cursor)
        return getopt(argc, argv, optstring);
    return long_option(argc, argv, optstring, longopts, longindex, 1);
}

int nanosleep(const struct timespec *request, struct timespec *remaining)
{
    if (!request || request->tv_sec < 0 || request->tv_nsec < 0 ||
        request->tv_nsec >= 1000000000L)
    {
        errno = EINVAL;
        return -1;
    }
    uint64_t ms = (uint64_t)request->tv_sec * 1000ULL +
                  (uint64_t)(request->tv_nsec + 999999L) / 1000000ULL;
    while (ms)
    {
        uint32_t step = ms > 0x7FFFFFFFULL ? 0x7FFFFFFFU : (uint32_t)ms;
        pc_sleep(step);
        ms -= step;
    }
    if (remaining)
    {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }
    return 0;
}

int usleep(unsigned int microseconds)
{
    if (microseconds)
        pc_sleep((microseconds + 999U) / 1000U);
    return 0;
}

int strerror_r(int code, char *buffer, size_t size)
{
    const char *text = strerror(code);
    if (!size)
        return ERANGE;
    strncpy(buffer, text, size - 1);
    buffer[size - 1] = '\0';
    return strlen(text) >= size ? ERANGE : 0;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

char *strcasestr(const char *haystack, const char *needle)
{
    if (!*needle)
        return (char *)haystack;
    for (; *haystack; haystack++)
    {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && lower(*h) == lower(*n))
        {
            h++;
            n++;
        }
        if (!*n)
            return (char *)haystack;
    }
    return 0;
}

size_t strlcpy(char *destination, const char *source, size_t size)
{
    size_t length = strlen(source);
    if (size)
    {
        size_t copy = length >= size ? size - 1 : length;
        memcpy(destination, source, copy);
        destination[copy] = '\0';
    }
    return length;
}

size_t strlcat(char *destination, const char *source, size_t size)
{
    size_t used = 0;
    while (used < size && destination[used])
        used++;
    if (used == size)
        return size + strlen(source);
    return used + strlcpy(destination + used, source, size - used);
}

void *memrchr(const void *buffer, int value, size_t count)
{
    const unsigned char *bytes = buffer;
    while (count--)
        if (bytes[count] == (unsigned char)value)
            return (void *)(bytes + count);
    return 0;
}

void *memmem(const void *haystack, size_t haystack_len, const void *needle, size_t needle_len)
{
    const unsigned char *h = haystack;
    const unsigned char *n = needle;
    if (!needle_len)
        return (void *)h;
    if (needle_len > haystack_len)
        return 0;
    for (size_t i = 0; i + needle_len <= haystack_len; i++)
        if (h[i] == n[0] && memcmp(h + i, n, needle_len) == 0)
            return (void *)(h + i);
    return 0;
}

size_t strxfrm(char *destination, const char *source, size_t count)
{
    size_t length = strlen(source);
    if (count)
    {
        size_t copy = length >= count ? count - 1 : length;
        memcpy(destination, source, copy);
        destination[copy] = '\0';
    }
    return length;
}

int gethostname(char *name, size_t length)
{
    const char *host = "purec";
    if (length < strlen(host) + 1)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(name, host);
    return 0;
}

long sysconf(int name)
{
    switch (name)
    {
    case _SC_PAGESIZE:
        return 4096;
    case _SC_CLK_TCK:
        return 100;
    case _SC_OPEN_MAX:
        return 1024;
    case _SC_ARG_MAX:
        return 131072;
    case _SC_CHILD_MAX:
        return 1024;
    case _SC_NGROUPS_MAX:
        return 32;
    case _SC_GETGR_R_SIZE_MAX:
    case _SC_GETPW_R_SIZE_MAX:
        return 1024;
    case _SC_LOGIN_NAME_MAX:
    case _SC_TTY_NAME_MAX:
        return 32;
    case _SC_HOST_NAME_MAX:
        return 64;
    case _SC_SYMLOOP_MAX:
        return 40;
    case _SC_PHYS_PAGES:
    case _SC_AVPHYS_PAGES:
    {
        struct memory_monitor_info memory;
        if (!pc_memory_info(&memory))
            return 0;
        uint64_t bytes = name == _SC_PHYS_PAGES ? memory.total_bytes : memory.available_bytes;
        return (long)(bytes / 4096);
    }
    case _SC_NPROCESSORS_CONF:
    case _SC_NPROCESSORS_ONLN:
    {
        struct cpu_monitor_info cpu;
        if (pc_cpu_info(&cpu) && cpu.logical_processors > 0)
            return (long)cpu.logical_processors;
        return 1;
    }
    default:
        errno = EINVAL;
        return -1;
    }
}

int fsync(int fd)
{
    (void)fd;
    return 0;
}

int fdatasync(int fd)
{
    (void)fd;
    return 0;
}

int truncate(const char *path, off_t length)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0)
        return -1;
    int result = ftruncate(fd, length);
    close(fd);
    return result;
}

ssize_t readlink(const char *path, char *buffer, size_t size)
{
    (void)path;
    (void)buffer;
    (void)size;
    errno = EINVAL;
    return -1;
}

int symlink(const char *target, const char *path)
{
    (void)target;
    (void)path;
    errno = EPERM;
    return -1;
}

int chown(const char *path, uid_t owner, gid_t group)
{
    (void)path;
    (void)owner;
    (void)group;
    return 0;
}

char *ttyname(int fd)
{
    return isatty(fd) ? "/dev/tty" : 0;
}

int fseeko(FILE *stream, off_t offset, int origin)
{
    return fseek(stream, (long)offset, origin);
}

off_t ftello(FILE *stream)
{
    return (off_t)ftell(stream);
}

ssize_t getdelim(char **line, size_t *capacity, int delimiter, FILE *stream)
{
    if (!line || !capacity || !stream)
    {
        errno = EINVAL;
        return -1;
    }
    if (!*line || !*capacity)
    {
        *capacity = 128;
        *line = malloc(*capacity);
        if (!*line)
            return -1;
    }
    size_t length = 0;
    int c;
    while ((c = fgetc(stream)) != EOF)
    {
        if (length + 2 > *capacity)
        {
            size_t grown = *capacity * 2;
            char *replacement = realloc(*line, grown);
            if (!replacement)
                return -1;
            *line = replacement;
            *capacity = grown;
        }
        (*line)[length++] = (char)c;
        if (c == delimiter)
            break;
    }
    if (!length)
        return -1;
    (*line)[length] = '\0';
    return (ssize_t)length;
}

ssize_t getline(char **line, size_t *capacity, FILE *stream)
{
    return getdelim(line, capacity, '\n', stream);
}

char *dirname(char *path)
{
    if (!path || !*path)
        return ".";
    size_t length = strlen(path);
    while (length > 1 && path[length - 1] == '/')
        path[--length] = '\0';
    char *slash = strrchr(path, '/');
    if (!slash)
        return ".";
    if (slash == path)
    {
        path[1] = '\0';
        return path;
    }
    while (slash > path && slash[-1] == '/')
        slash--;
    *slash = '\0';
    return path;
}

char *basename(char *path)
{
    if (!path || !*path)
        return ".";
    size_t length = strlen(path);
    while (length > 1 && path[length - 1] == '/')
        path[--length] = '\0';
    char *slash = strrchr(path, '/');
    return slash && length > 1 ? slash + 1 : path;
}

static int match_here(const char *pattern, const char *text, int flags)
{
    for (; *pattern; pattern++, text++)
    {
        switch (*pattern)
        {
        case '*':
            while (pattern[1] == '*')
                pattern++;
            for (const char *t = text;; t++)
            {
                if (match_here(pattern + 1, t, flags) == 0)
                    return 0;
                if (!*t || ((flags & FNM_PATHNAME) && *t == '/'))
                    return FNM_NOMATCH;
            }
        case '?':
            if (!*text || ((flags & FNM_PATHNAME) && *text == '/'))
                return FNM_NOMATCH;
            break;
        case '[':
        {
            if (!*text)
                return FNM_NOMATCH;
            const char *p = pattern + 1;
            int negate = *p == '!' || *p == '^';
            if (negate)
                p++;
            int matched = 0;
            int first = 1;
            while (*p && (*p != ']' || first))
            {
                first = 0;
                if (p[1] == '-' && p[2] && p[2] != ']')
                {
                    if (*text >= p[0] && *text <= p[2])
                        matched = 1;
                    p += 3;
                }
                else
                {
                    if (*text == *p)
                        matched = 1;
                    p++;
                }
            }
            if (!*p || matched == negate)
                return FNM_NOMATCH;
            pattern = p;
            break;
        }
        case '\\':
            if (!(flags & FNM_NOESCAPE) && pattern[1])
                pattern++;
            /* fallthrough */
        default:
            if ((flags & FNM_CASEFOLD) ? lower(*pattern) != lower(*text)
                                       : *pattern != *text)
                return FNM_NOMATCH;
        }
    }
    return *text ? FNM_NOMATCH : 0;
}

int fnmatch(const char *pattern, const char *text, int flags)
{
    return match_here(pattern, text, flags);
}

static unsigned long long random_state = 1;

static unsigned int random_next(void)
{
    random_state = random_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (unsigned int)(random_state >> 33);
}

void srandom(unsigned int seed) { random_state = seed ? seed : 1; }
long random(void) { return (long)random_next(); }
void srand48(long seed) { srandom((unsigned int)seed); }
long lrand48(void) { return (long)random_next(); }
long mrand48(void) { return (long)(int)random_next(); }
double drand48(void) { return (double)random_next() / 2147483648.0; }

int rand_r(unsigned int *seed)
{
    *seed = *seed * 1103515245U + 12345U;
    return (int)((*seed >> 16) & 0x7FFF);
}

long long llabs(long long value) { return value < 0 ? -value : value; }

div_t div(int numerator, int denominator)
{
    div_t r = {numerator / denominator, numerator % denominator};
    return r;
}

ldiv_t ldiv(long numerator, long denominator)
{
    ldiv_t r = {numerator / denominator, numerator % denominator};
    return r;
}

lldiv_t lldiv(long long numerator, long long denominator)
{
    lldiv_t r = {numerator / denominator, numerator % denominator};
    return r;
}

static void swap_bytes(unsigned char *a, unsigned char *b, size_t size)
{
    while (size--)
    {
        unsigned char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

void qsort_r(void *base, size_t count, size_t size,
             int (*compare)(const void *, const void *, void *), void *context)
{
    unsigned char *items = base;
    for (size_t gap = count / 2; gap > 0; gap /= 2)
        for (size_t i = gap; i < count; i++)
            for (size_t j = i; j >= gap &&
                 compare(items + (j - gap) * size, items + j * size, context) > 0;
                 j -= gap)
                swap_bytes(items + (j - gap) * size, items + j * size, size);
}

ssize_t pread(int fd, void *buffer, size_t count, off_t offset)
{
    off_t saved = lseek(fd, 0, SEEK_CUR);
    if (saved < 0)
        return -1;
    if (lseek(fd, offset, SEEK_SET) < 0)
        return -1;
    ssize_t got = read(fd, buffer, count);
    int saved_errno = errno;
    lseek(fd, saved, SEEK_SET);
    errno = saved_errno;
    return got;
}

ssize_t pwrite(int fd, const void *buffer, size_t count, off_t offset)
{
    off_t saved = lseek(fd, 0, SEEK_CUR);
    if (saved < 0)
        return -1;
    if (lseek(fd, offset, SEEK_SET) < 0)
        return -1;
    ssize_t put = write(fd, buffer, count);
    int saved_errno = errno;
    lseek(fd, saved, SEEK_SET);
    errno = saved_errno;
    return put;
}

char *strchrnul(const char *text, int ch)
{
    while (*text && *text != (char)ch)
        text++;
    return (char *)text;
}
