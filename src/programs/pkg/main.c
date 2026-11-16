#include "pkgd.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PKG_VERSION_TEXT "pkg 1.0"

static int lock_held;

static void lock_release(void)
{
    if (lock_held)
        unlink(rp("var/lib/pkg/db.lck"));
    lock_held = 0;
}

static void lock_acquire(void)
{
    if (mkdirs(rp("var/lib/pkg"), 0755) < 0 && !is_directory(rp("var/lib/pkg")))
        die("cannot create %s: %s", rp("var/lib/pkg"), strerror(errno));
    int fd = open(rp("var/lib/pkg/db.lck"), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0 && errno == EEXIST) {
        char text[32] = "";
        int old = open(rp("var/lib/pkg/db.lck"), O_RDONLY);
        if (old >= 0) {
            ssize_t n = read(old, text, sizeof(text) - 1);
            text[n > 0 ? n : 0] = '\0';
            close(old);
        }
        long pid = atol(text);
        if (pid <= 0 || (kill((pid_t)pid, 0) < 0 && errno == ESRCH)) {
            warn("removing a stale lock left by %s", pid > 0 ? "a pkg that is no longer running" : "an interrupted pkg");
            unlink(rp("var/lib/pkg/db.lck"));
            fd = open(rp("var/lib/pkg/db.lck"), O_WRONLY | O_CREAT | O_EXCL, 0644);
        }
    }
    if (fd < 0) {
        if (errno == EEXIST)
            die("the database is locked by another pkg, remove %s if no other pkg is running", rp("var/lib/pkg/db.lck"));
        if (errno == EACCES || errno == EPERM)
            die("permission denied (run as root)");
        die("cannot lock the database: %s", strerror(errno));
    }
    char me[24];
    int n = snprintf(me, sizeof(me), "%ld\n", (long)getpid());
    if (n > 0)
        (void)write(fd, me, (size_t)n);
    close(fd);
    lock_held = 1;
    atexit(lock_release);
}

static void usage(void)
{
    printf("usage: pkg <operation> [options] [targets]\n\n"
           "operations:\n"
           "  -S [targets]    install packages from the repositories of the registry\n"
           "                  (a target is a package name or its GitHub repository URL)\n"
           "      -y          download the registry and check the latest releases (-Sy, -Syu)\n"
           "      -u          upgrade every package that has a newer version (-Su, -Syu)\n"
           "      -s TERM     search the repositories\n"
           "      -i NAME     show information about a repository package\n"
           "      -l          list the registry, its repositories and their packages\n"
           "      -c          remove cached packages that are not installed\n"
           "  -R targets      remove packages (-Rs also removes unneeded dependencies)\n"
           "  -Q [targets]    query installed packages\n"
           "      -i NAME     information, -l files, -o FILE owner, -e explicit only\n"
           "      -d only dependencies, -s TERM search, -u upgradable, -k check files, -q names only\n"
           "  -U files        install package files\n"
           "  -D names        change the install reason (--asdeps or --asexplicit)\n\n"
           "options:\n"
           "  --root DIR      operate on another root directory\n"
           "  --config FILE   use another configuration file\n"
           "  --noconfirm     do not ask questions\n"
           "  --needed        do not reinstall up-to-date packages\n"
           "  --force         overwrite files that exist and belong to no package\n"
           "  --nodeps        skip dependency checks\n"
           "  --asdeps        mark installed packages as dependencies\n"
           "  --asexplicit    mark installed packages as explicitly installed\n"
           "  -v, --version   show the version\n");
}

static int contains_icase(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (!n)
        return 1;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == n)
            return 1;
    }
    return 0;
}

static void print_depends(const struct pkg_info *p)
{
    printf("Depends On      :");
    if (!p->depend_count)
        printf(" None");
    for (int i = 0; i < p->depend_count; i++)
        printf(" %s", p->depends[i]);
    putchar('\n');
}

static void print_info(const struct pkg_info *p, const char *repo, int installed)
{
    char sz[32];
    printf("Name            : %s\n", p->name);
    printf("Version         : %s\n", p->version);
    printf("Description     : %s\n", p->desc[0] ? p->desc : "None");
    printf("Architecture    : %s\n", p->arch[0] ? p->arch : "any");
    print_depends(p);
    printf("Conflicts With  :");
    if (!p->conflict_count)
        printf(" None");
    for (int i = 0; i < p->conflict_count; i++)
        printf(" %s", p->conflicts[i]);
    putchar('\n');
    human_size(p->size, sz, sizeof(sz));
    printf("Installed Size  : %s\n", sz);
    if (repo) {
        human_size(p->csize, sz, sizeof(sz));
        printf("Download Size   : %s\n", sz);
        printf("Repository      : %s\n", repo);
    }
    if (installed) {
        printf("Install Reason  : %s\n", p->reason == 1 ? "Installed as a dependency" : "Explicitly installed");
        char when[64] = "unknown";
        time_t t = (time_t)p->installdate;
        struct tm *tm = gmtime(&t);
        if (tm && p->installdate)
            strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S UTC", tm);
        printf("Install Date    : %s\n", when);
    }
    putchar('\n');
}

static int op_query(int argc, char **targets, int flags_i, int flags_l, int flags_o, int flags_e, int flags_d,
                    int flags_s, int flags_u, int flags_k, int quiet)
{
    localdb_load();
    if (flags_u) {
        config_load();
        syncdb_load();
        list_upgrades(quiet);
        return 0;
    }
    if (flags_o) {
        int status = 0;
        for (int i = 0; i < argc; i++) {
            const char *path = targets[i];
            size_t root_len = strlen(opt.root);
            if (root_len > 1 && !strncmp(path, opt.root, root_len) && path[root_len] == '/')
                path += root_len;
            while (*path == '/')
                path++;
            const char *owner = localdb_owner(path);
            if (!owner) {
                fprintf(stderr, "error: no package owns /%s\n", path);
                status = 1;
                continue;
            }
            int idx = localdb_find(owner);
            printf("/%s is owned by %s %s\n", path, owner, idx >= 0 ? ldb.pkgs[idx].version : "");
        }
        return status;
    }
    if (flags_i || flags_l || flags_k) {
        int status = 0;
        if (!argc)
            die("no targets given");
        for (int i = 0; i < argc; i++) {
            int idx = localdb_find(targets[i]);
            if (idx < 0) {
                fprintf(stderr, "error: package '%s' was not found\n", targets[i]);
                status = 1;
                continue;
            }
            if (flags_i)
                print_info(&ldb.pkgs[idx], NULL, 1);
            if (flags_l || flags_k) {
                int count;
                char **files = localdb_files(&ldb.pkgs[idx], &count);
                int missing = 0;
                for (int j = 0; j < count; j++) {
                    if (flags_l) {
                        if (quiet)
                            printf("/%s\n", files[j]);
                        else
                            printf("%s /%s\n", ldb.pkgs[idx].name, files[j]);
                    } else {
                        size_t n = strlen(files[j]);
                        char clean[PKG_PATH_MAX + 2];
                        snprintf(clean, sizeof(clean), "%s", files[j]);
                        if (n && clean[n - 1] == '/')
                            clean[n - 1] = '\0';
                        if (!path_exists(rp(clean))) {
                            printf("%s: /%s (No such file or directory)\n", ldb.pkgs[idx].name, clean);
                            missing++;
                        }
                    }
                }
                if (flags_k) {
                    printf("%s: %d total files, %d missing files\n", ldb.pkgs[idx].name, count, missing);
                    if (missing)
                        status = 1;
                }
                free_list(files, count);
            }
        }
        return status;
    }
    for (int i = 0; i < ldb.count; i++) {
        const struct pkg_info *p = &ldb.pkgs[i];
        if (flags_e && p->reason == 1)
            continue;
        if (flags_d && p->reason != 1)
            continue;
        if (argc) {
            int match = 0;
            for (int j = 0; j < argc; j++)
                if (flags_s ? (contains_icase(p->name, targets[j]) || contains_icase(p->desc, targets[j])) : !strcmp(p->name, targets[j]))
                    match = 1;
            if (!match)
                continue;
        }
        if (quiet)
            printf("%s\n", p->name);
        else
            printf("%s %s\n", p->name, p->version);
    }
    return 0;
}

static int op_clean_cache(void)
{
    localdb_load();
    DIR *d = opendir(rp("var/cache/pkg"));
    if (!d)
        return 0;
    struct dirent *e;
    int removed = 0;
    uint64_t freed = 0;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        int keep = 0;
        for (int i = 0; i < ldb.count; i++) {
            size_t fl = strlen(ldb.pkgs[i].filename);
            if (!strcmp(ldb.pkgs[i].filename, e->d_name) ||
                (fl && !strncmp(ldb.pkgs[i].filename, e->d_name, fl) && !strcmp(e->d_name + fl, ".sig")))
                keep = 1;
        }
        if (keep)
            continue;
        char path[PKG_PATH_MAX + 300];
        snprintf(path, sizeof(path), "%s/%s", rp("var/cache/pkg"), e->d_name);
        struct stat st;
        if (stat(path, &st) == 0)
            freed += (uint64_t)st.st_size;
        if (unlink(path) == 0)
            removed++;
    }
    closedir(d);
    char sz[32];
    human_size(freed, sz, sizeof(sz));
    printf("removed %d cached package(s), %s freed\n", removed, sz);
    return 0;
}

static int op_database(char **targets, int count, int reason)
{
    localdb_load();
    for (int i = 0; i < count; i++) {
        int idx = localdb_find(targets[i]);
        if (idx < 0)
            die("package '%s' was not found", targets[i]);
        struct pkg_info info = ldb.pkgs[idx];
        info.reason = reason;
        int n;
        char **files = localdb_files(&info, &n);
        if (localdb_write(&info, files, n) < 0)
            die("cannot update the database for %s", info.name);
        free_list(files, n);
        printf("%s: install reason set to %s\n", info.name, reason ? "dependency" : "explicit");
    }
    return 0;
}

int main(int argc, char **argv)
{
    snprintf(opt.root, sizeof(opt.root), "/");
    char op = 0;
    int y = 0, u = 0, s = 0, i = 0, l = 0, c = 0, o = 0, e = 0, d = 0, k = 0;
    char *targets[512];
    int ntargets = 0;
    for (int a = 1; a < argc; a++) {
        const char *arg = argv[a];
        if (arg[0] == '-' && arg[1] == '-') {
            if (!strcmp(arg, "--noconfirm"))
                opt.noconfirm = 1;
            else if (!strcmp(arg, "--needed"))
                opt.needed = 1;
            else if (!strcmp(arg, "--force"))
                opt.force = 1;
            else if (!strcmp(arg, "--nodeps"))
                opt.nodeps = 1;
            else if (!strcmp(arg, "--asdeps"))
                opt.asdeps = 1;
            else if (!strcmp(arg, "--asexplicit"))
                opt.asexplicit = 1;
            else if (!strcmp(arg, "--verbose"))
                opt.verbose = 1;
            else if (!strcmp(arg, "--version")) {
                puts(PKG_VERSION_TEXT);
                return 0;
            } else if (!strcmp(arg, "--help")) {
                usage();
                return 0;
            } else if (!strcmp(arg, "--root") && a + 1 < argc)
                snprintf(opt.root, sizeof(opt.root), "%s", argv[++a]);
            else if (!strncmp(arg, "--root=", 7))
                snprintf(opt.root, sizeof(opt.root), "%s", arg + 7);
            else if (!strcmp(arg, "--config") && a + 1 < argc)
                opt.config = argv[++a];
            else if (!strncmp(arg, "--config=", 9))
                opt.config = arg + 9;
            else
                die("unknown option '%s'", arg);
            continue;
        }
        if (arg[0] == '-' && arg[1]) {
            for (const char *p = arg + 1; *p; p++) {
                switch (*p) {
                case 'S': case 'R': case 'Q': case 'U': case 'D':
                    if (op && op != *p)
                        die("only one operation may be given");
                    op = *p;
                    break;
                case 'y': y++; break;
                case 'u': u++; break;
                case 's': s++; break;
                case 'i': i++; break;
                case 'l': l++; break;
                case 'c': c++; break;
                case 'o': o++; break;
                case 'e': e++; break;
                case 'd': d++; break;
                case 'k': k++; break;
                case 'q': opt.quiet = 1; break;
                case 'v': puts(PKG_VERSION_TEXT); return 0;
                case 'h': usage(); return 0;
                default: die("unknown option '-%c'", *p);
                }
            }
            continue;
        }
        if (ntargets == 512)
            die("too many targets");
        targets[ntargets++] = argv[a];
    }
    if (!op) {
        usage();
        return 1;
    }
    size_t rl = strlen(opt.root);
    while (rl > 1 && opt.root[rl - 1] == '/')
        opt.root[--rl] = '\0';
    if (op == 'R' && d)
        opt.nodeps = 1;
    if (op == 'Q')
        return op_query(ntargets, targets, i, l, o, e, d, s, u, k, opt.quiet);
    if (op == 'S' && (s || i || l)) {
        config_load();
        syncdb_load();
        int status = 0;
        if (!sdb.count)
            warn("nothing is known about the trusted repositories yet, run 'pkg -Sy' first");
        if (s) {
            for (int n = 0; n < sdb.count; n++)
                for (int t = 0; t < ntargets; t++)
                    if (contains_icase(sdb.pkgs[n].name, targets[t]) || contains_icase(sdb.pkgs[n].desc, targets[t])) {
                        printf("%s/%s %s\n    %s\n", sdb.meta[n].repo, sdb.pkgs[n].name, sdb.pkgs[n].version, sdb.pkgs[n].desc);
                        break;
                    }
        } else if (i) {
            for (int t = 0; t < ntargets; t++) {
                int idx = syncdb_find(targets[t]);
                if (idx < 0) {
                    fprintf(stderr, "error: package '%s' was not found\n", targets[t]);
                    status = 1;
                } else {
                {
                    char where[128];
                    snprintf(where, sizeof(where), "%s%s", sdb.meta[idx].repo,
                             sdb.meta[idx].local ? " (local, not from the registry)" : "");
                    print_info(&sdb.pkgs[idx], where, 0);
                }
                }
            }
        } else {
            list_sources();
        }
        return status;
    }
    if (op == 'S' && c)
        return op_clean_cache();
    lock_acquire();
    int status = 0;
    if (op == 'S') {
        config_load();
        if (y && sync_refresh() < 0) {
            status = 1;
            if (ntargets || u)
                warn("some trusted repositories could not be checked, continuing with the others");
        }
        if (ntargets || u) {
            localdb_load();
            syncdb_load();
            if (!sdb.count && u && !ntargets)
                die("nothing is known about the trusted repositories yet, run 'pkg -Sy' first");
            if (u)
                tx_upgrade(0);
            if (ntargets)
                tx_install(targets, ntargets, 0);
        } else if (!y) {
            usage();
            status = 1;
        }
    } else if (op == 'R') {
        if (!ntargets)
            die("no targets given");
        localdb_load();
        tx_remove(targets, ntargets, s);
    } else if (op == 'U') {
        if (!ntargets)
            die("no package files given");
        config_load();
        localdb_load();
        syncdb_load();
        tx_install(targets, ntargets, 1);
    } else if (op == 'D') {
        if (!ntargets || (!opt.asdeps && !opt.asexplicit))
            die("give package names and --asdeps or --asexplicit");
        status = op_database(targets, ntargets, opt.asdeps ? 1 : 0);
    }
    return status;
}
