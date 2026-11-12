#include "../lib/common.h"

int main(int argc, char **argv)
{
    util_init(argc, argv);
    bool all = false;
    int i = 1;
    if (i < argc && !strcmp(argv[i], "-a")) { all = true; i++; }
    if (i < argc && !strcmp(argv[i], "--help")) { util_usage("[-a] COMMAND..."); return 0; }
    if (i >= argc) return 1;
    const char *path = getenv("PATH");
    if (!path) path = "/usr/bin:/bin:/usr/games:/sbin";
    int rc = 0;
    for (; i < argc; i++) {
        bool found = false;
        if (strchr(argv[i], '/')) {
            if (access(argv[i], X_OK) == 0 || util_exists(argv[i])) { puts(argv[i]); found = true; }
        } else {
            const char *p = path;
            for (;;) {
                const char *colon = strchr(p, ':');
                size_t len = colon ? (size_t)(colon - p) : strlen(p);
                char dir[UTIL_PATH], full[UTIL_PATH];
                snprintf(dir, sizeof(dir), "%.*s", (int)len, len ? p : ".");
                util_join(full, sizeof(full), dir, argv[i]);
                struct stat st;
                if (stat(full, &st) == 0 && !S_ISDIR(st.st_mode)) {
                    puts(full);
                    found = true;
                    if (!all) break;
                }
                if (!colon) break;
                p = colon + 1;
            }
        }
        if (!found) rc = 1;
    }
    return rc;
}
