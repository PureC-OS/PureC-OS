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
    case _SC_NPROCESSORS_CONF:
    case _SC_NPROCESSORS_ONLN:
        return 1;
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

char *nl_langinfo(nl_item item)
{
    (void)item;
    return "UTF-8";
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

size_t wcslen(const wchar_t *text)
{
    size_t length = 0;
    while (text[length])
        length++;
    return length;
}

size_t mbrtowc(wchar_t *out, const char *text, size_t count, mbstate_t *state)
{
    (void)state;
    if (!text)
        return 0;
    if (!count)
        return (size_t)-2;
    unsigned char lead = (unsigned char)text[0];
    unsigned int value;
    size_t need;
    if (lead < 0x80)
    {
        value = lead;
        need = 1;
    }
    else if ((lead & 0xE0) == 0xC0)
    {
        value = lead & 0x1F;
        need = 2;
    }
    else if ((lead & 0xF0) == 0xE0)
    {
        value = lead & 0x0F;
        need = 3;
    }
    else if ((lead & 0xF8) == 0xF0)
    {
        value = lead & 0x07;
        need = 4;
    }
    else
    {
        errno = EILSEQ;
        return (size_t)-1;
    }
    if (count < need)
        return (size_t)-2;
    for (size_t i = 1; i < need; i++)
    {
        if (((unsigned char)text[i] & 0xC0) != 0x80)
        {
            errno = EILSEQ;
            return (size_t)-1;
        }
        value = (value << 6) | ((unsigned char)text[i] & 0x3F);
    }
    if (out)
        *out = (wchar_t)value;
    return value ? need : 0;
}

size_t wcrtomb(char *out, wchar_t value, mbstate_t *state)
{
    (void)state;
    unsigned int c = (unsigned int)value;
    if (!out)
        return 1;
    if (c < 0x80)
    {
        out[0] = (char)c;
        return 1;
    }
    if (c < 0x800)
    {
        out[0] = (char)(0xC0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c < 0x10000)
    {
        out[0] = (char)(0xE0 | (c >> 12));
        out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        out[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    if (c < 0x110000)
    {
        out[0] = (char)(0xF0 | (c >> 18));
        out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        out[3] = (char)(0x80 | (c & 0x3F));
        return 4;
    }
    errno = EILSEQ;
    return (size_t)-1;
}

int mbsinit(const mbstate_t *state)
{
    return !state || !state->state;
}

size_t mbstowcs(wchar_t *out, const char *text, size_t count)
{
    size_t produced = 0;
    while (*text)
    {
        wchar_t value;
        size_t used = mbrtowc(&value, text, 4, 0);
        if (used == (size_t)-1 || used == (size_t)-2)
            return (size_t)-1;
        if (out)
        {
            if (produced >= count)
                return produced;
            out[produced] = value;
        }
        produced++;
        text += used;
    }
    if (out && produced < count)
        out[produced] = 0;
    return produced;
}

size_t wcstombs(char *out, const wchar_t *text, size_t count)
{
    size_t produced = 0;
    char piece[4];
    while (*text)
    {
        size_t used = wcrtomb(piece, *text++, 0);
        if (used == (size_t)-1)
            return (size_t)-1;
        if (out)
        {
            if (produced + used > count)
                return produced;
            memcpy(out + produced, piece, used);
        }
        produced += used;
    }
    if (out && produced < count)
        out[produced] = '\0';
    return produced;
}

#define TLS_KEYS 64
static void *key_values[TLS_KEYS];
static unsigned char key_used[TLS_KEYS];

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start)(void *), void *argument)
{
    (void)thread;
    (void)attr;
    (void)start;
    (void)argument;
    return EAGAIN;
}

int pthread_join(pthread_t thread, void **result)
{
    (void)thread;
    (void)result;
    return ESRCH;
}

int pthread_detach(pthread_t thread)
{
    (void)thread;
    return ESRCH;
}

pthread_t pthread_self(void) { return 1; }
int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    mutex->locked = 0;
    mutex->kind = attr ? attr->kind : 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (mutex->locked && mutex->kind != PTHREAD_MUTEX_RECURSIVE)
        return mutex->kind == PTHREAD_MUTEX_ERRORCHECK ? EDEADLK : 0;
    mutex->locked++;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    if (mutex->locked && mutex->kind != PTHREAD_MUTEX_RECURSIVE)
        return EBUSY;
    mutex->locked++;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (mutex->locked)
        mutex->locked--;
    return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    attr->kind = 0;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    (void)attr;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int kind)
{
    attr->kind = kind;
    return 0;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    (void)attr;
    cond->waiters = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    (void)cond;
    (void)mutex;
    return EDEADLK;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *deadline)
{
    (void)cond;
    (void)mutex;
    (void)deadline;
    return ETIMEDOUT;
}

int pthread_once(pthread_once_t *once, void (*routine)(void))
{
    if (!*once)
    {
        *once = 1;
        routine();
    }
    return 0;
}

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    (void)destructor;
    for (unsigned i = 0; i < TLS_KEYS; i++)
    {
        if (!key_used[i])
        {
            key_used[i] = 1;
            key_values[i] = 0;
            *key = i;
            return 0;
        }
    }
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key)
{
    if (key >= TLS_KEYS)
        return EINVAL;
    key_used[key] = 0;
    return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
    return key < TLS_KEYS && key_used[key] ? key_values[key] : 0;
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
    if (key >= TLS_KEYS || !key_used[key])
        return EINVAL;
    key_values[key] = (void *)value;
    return 0;
}

int pthread_rwlock_init(pthread_rwlock_t *lock, const void *attr)
{
    (void)attr;
    lock->readers = 0;
    lock->writer = 0;
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *lock)
{
    (void)lock;
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *lock)
{
    lock->readers++;
    return 0;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *lock)
{
    lock->writer = 1;
    return 0;
}

int pthread_rwlock_unlock(pthread_rwlock_t *lock)
{
    if (lock->writer)
        lock->writer = 0;
    else if (lock->readers)
        lock->readers--;
    return 0;
}

int pthread_attr_init(pthread_attr_t *attr)
{
    attr->dummy = 0;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
    (void)attr;
    return 0;
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

#include "include/hosted/regex.h"

int regcomp(regex_t *preg, const char *pattern, int cflags)
{
    (void)pattern;
    (void)cflags;
    if (preg)
        preg->re_nsub = 0;
    return 0;
}

int regexec(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags)
{
    (void)preg;
    (void)string;
    (void)nmatch;
    (void)pmatch;
    (void)eflags;
    return REG_NOMATCH;
}

size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size)
{
    (void)errcode;
    (void)preg;
    if (errbuf && errbuf_size)
        *errbuf = 0;
    return 0;
}

void regfree(regex_t *preg)
{
    (void)preg;
}
