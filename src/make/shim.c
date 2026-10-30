#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *find_in_given_path(const char *progname, const char *path,
                               const char *directory, bool optimize_for_exec) {
    (void)directory;
    (void)optimize_for_exec;
    if (strchr(progname, '/')) {
        return progname;
    }
    if (!path) path = "";
    size_t name_length = strlen(progname);
    const char *cursor = path;
    for (;;) {
        const char *end = strchr(cursor, ':');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        char *candidate = (char *)malloc(length + name_length + 3);
        if (!candidate) { errno = ENOMEM; return 0; }
        if (length) memcpy(candidate, cursor, length);
        else { candidate[0] = '.'; length = 1; }
        candidate[length] = '/';
        memcpy(candidate + length + 1, progname, name_length + 1);
        struct stat info;
        if (stat(candidate, &info) == 0 && !S_ISDIR(info.st_mode)) return candidate;
        free(candidate);
        if (!end) break;
        cursor = end + 1;
    }
    errno = ENOENT;
    return 0;
}
