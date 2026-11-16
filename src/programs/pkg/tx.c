#include "pkgd.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct item {
    struct pkg_info info;
    char file[PKG_PATH_MAX];
    int sync_index;
    int old;
    int reason;
};

struct plan {
    struct item *items;
    int count;
    int cap;
};

struct package_scan {
    struct pkg_info info;
    char **files;
    int file_count;
    int file_cap;
    uint64_t total_size;
};

static void list_add(char ***list, int *count, int *cap, const char *text)
{
    if (*count == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        *list = realloc(*list, (size_t)*cap * sizeof(char *));
        if (!*list)
            die("out of memory");
    }
    (*list)[(*count)++] = strdup(text);
}

static int str_compare(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int list_has(char **sorted, int count, const char *text)
{
    return count && bsearch(&text, sorted, (size_t)count, sizeof(char *), str_compare) != NULL;
}

static int is_meta(const char *path)
{
    return path[0] == '.' && !strchr(path, '/');
}

static void scan_free(struct package_scan *s)
{
    free_list(s->files, s->file_count);
    s->files = NULL;
    s->file_count = 0;
}

static int scan_package(const char *file, struct package_scan *s)
{
    memset(s, 0, sizeof(*s));
    struct pkg_archive *a = pkg_archive_open(file);
    if (!a)
        return -1;
    struct pkg_entry e;
    int have_info = 0, rc;
    while ((rc = pkg_archive_next(a, &e)) == 1) {
        if (!strcmp(e.path, ".PKGINFO")) {
            if (e.size > 32768)
                break;
            char text[32769];
            long got = pkg_archive_read(a, text, e.size);
            if (got < 0 || (uint64_t)got != e.size)
                break;
            text[got] = '\0';
            if (pkg_info_parse(text, &s->info) < 0)
                break;
            have_info = 1;
            continue;
        }
        if (is_meta(e.path))
            continue;
        char entry[PKG_PATH_MAX + 2];
        snprintf(entry, sizeof(entry), "%s%s", e.path, e.type == '5' ? "/" : "");
        list_add(&s->files, &s->file_count, &s->file_cap, entry);
        if (e.type == '0')
            s->total_size += e.size;
    }
    pkg_archive_close(a);
    if (rc < 0 || !have_info) {
        scan_free(s);
        return -1;
    }
    if (!s->info.size)
        s->info.size = s->total_size;
    return 0;
}

int sync_complete(int index)
{
    if (index < 0 || index >= sdb.count)
        return -1;
    struct sync_meta *m = &sdb.meta[index];
    if (m->complete)
        return 0;
    struct pkg_info *p = &sdb.pkgs[index];
    char file[PKG_PATH_MAX];
    if (fetch_package(index, file, sizeof(file)) < 0)
        die("failed to retrieve %s from %s", p->name, m->repo);
    struct package_scan scan;
    if (scan_package(file, &scan) < 0)
        die("%s: %s is not a valid package", p->name, file);
    scan_free(&scan);
    if (strcmp(scan.info.name, p->name) != 0)
        die("%s: the release asset contains the package '%s'", p->name, scan.info.name);
    if (strcmp(scan.info.version, p->version) != 0)
        warn("%s: asset name says %s, the package says %s", p->name, p->version, scan.info.version);
    struct stat st;
    scan.info.csize = stat(file, &st) == 0 ? (uint64_t)st.st_size : p->csize;
    snprintf(scan.info.filename, sizeof(scan.info.filename), "%s", p->filename);
    if (!scan.info.desc[0])
        snprintf(scan.info.desc, sizeof(scan.info.desc), "%s", p->desc);
    hash_file(file, scan.info.sha512);
    *p = scan.info;
    snprintf(m->file, sizeof(m->file), "%s", file);
    m->complete = 1;
    return 0;
}

static int plan_find(const struct plan *p, const char *name)
{
    for (int i = 0; i < p->count; i++)
        if (!strcmp(p->items[i].info.name, name))
            return i;
    return -1;
}

static struct item *plan_push(struct plan *p)
{
    if (p->count == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 16;
        p->items = realloc(p->items, (size_t)p->cap * sizeof(*p->items));
        if (!p->items)
            die("out of memory");
    }
    struct item *it = &p->items[p->count++];
    memset(it, 0, sizeof(*it));
    it->old = -1;
    it->sync_index = -1;
    return it;
}

static int visiting_depth;
static char visiting[64][PKG_NAME_MAX];

static void add_package(struct plan *plan, const struct pkg_info *info, int sync_index, const char *file, int reason,
                        const char *required_by);

static void resolve_dependencies(struct plan *plan, const struct pkg_info *info)
{
    if (opt.nodeps)
        return;
    for (int i = 0; i < info->depend_count; i++) {
        char name[PKG_NAME_MAX], op[8], version[PKG_VERSION_MAX];
        if (pkg_dep_split(info->depends[i], name, sizeof(name), op, sizeof(op), version, sizeof(version)) < 0)
            die("%s: malformed dependency '%s'", info->name, info->depends[i]);
        int in_plan = plan_find(plan, name);
        if (in_plan >= 0) {
            if (!pkg_dep_satisfied(op, version, plan->items[in_plan].info.version))
                die("%s requires %s but %s-%s is selected", info->name, info->depends[i], name,
                    plan->items[in_plan].info.version);
            continue;
        }
        int installed = localdb_find(name);
        if (installed >= 0 && pkg_dep_satisfied(op, version, ldb.pkgs[installed].version))
            continue;
        int s = syncdb_find(name);
        if (s < 0)
            die("unable to satisfy dependency '%s' required by %s (no trusted repository for it)", info->depends[i],
                info->name);
        sync_complete(s);
        if (!pkg_dep_satisfied(op, version, sdb.pkgs[s].version))
            die("dependency '%s' required by %s is not met by %s-%s in the repository", info->depends[i], info->name,
                name, sdb.pkgs[s].version);
        add_package(plan, &sdb.pkgs[s], s, NULL, 1, info->name);
    }
}

static void add_package(struct plan *plan, const struct pkg_info *info, int sync_index, const char *file, int reason,
                        const char *required_by)
{
    (void)required_by;
    if (plan_find(plan, info->name) >= 0)
        return;
    for (int i = 0; i < visiting_depth; i++)
        if (!strcmp(visiting[i], info->name))
            return;
    if (visiting_depth < 64)
        snprintf(visiting[visiting_depth++], PKG_NAME_MAX, "%s", info->name);
    resolve_dependencies(plan, info);
    visiting_depth--;
    struct item *it = plan_push(plan);
    it->info = *info;
    it->sync_index = sync_index;
    it->old = localdb_find(info->name);
    it->reason = reason;
    if (file)
        snprintf(it->file, sizeof(it->file), "%s", file);
    else if (sync_index >= 0)
        snprintf(it->file, sizeof(it->file), "%s", sdb.meta[sync_index].file);
    if (it->old >= 0 && reason == 1)
        it->reason = ldb.pkgs[it->old].reason;
}

static void check_conflicts(const struct plan *plan)
{
    for (int i = 0; i < plan->count; i++) {
        const struct pkg_info *p = &plan->items[i].info;
        for (int c = 0; c < p->conflict_count; c++) {
            int installed = localdb_find(p->conflicts[c]);
            if (installed >= 0 && plan_find(plan, p->conflicts[c]) < 0)
                die("%s conflicts with the installed package %s", p->name, p->conflicts[c]);
            int other = plan_find(plan, p->conflicts[c]);
            if (other >= 0 && other != i)
                die("%s conflicts with %s", p->name, p->conflicts[c]);
        }
        for (int j = 0; j < ldb.count; j++) {
            if (!strcmp(ldb.pkgs[j].name, p->name))
                continue;
            for (int c = 0; c < ldb.pkgs[j].conflict_count; c++)
                if (!strcmp(ldb.pkgs[j].conflicts[c], p->name))
                    die("%s conflicts with the installed package %s", p->name, ldb.pkgs[j].name);
        }
    }
}

static int check_files(const struct item *it, const struct package_scan *scan, char **old_sorted, int old_count)
{
    int bad = 0;
    for (int i = 0; i < scan->file_count; i++) {
        const char *path = scan->files[i];
        size_t n = strlen(path);
        int is_dir = n && path[n - 1] == '/';
        char clean[PKG_PATH_MAX + 2];
        snprintf(clean, sizeof(clean), "%s", path);
        if (is_dir)
            clean[n - 1] = '\0';
        const char *dest = rp(clean);
        struct stat st;
        if (lstat(dest, &st) < 0)
            continue;
        if (is_dir) {
            if (!S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
                warn("%s: %s exists and is not a directory", it->info.name, clean);
                bad++;
            }
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            warn("%s: %s is a directory on disk", it->info.name, clean);
            bad++;
            continue;
        }
        if (list_has(old_sorted, old_count, path))
            continue;
        const char *owner = localdb_owner(path);
        if (owner && strcmp(owner, it->info.name) != 0) {
            warn("%s: %s is owned by %s", it->info.name, clean, owner);
            bad++;
        } else if (!owner && !opt.force) {
            warn("%s: %s exists in the filesystem", it->info.name, clean);
            bad++;
        }
    }
    return bad;
}

static char **load_old_files(const struct item *it, int *count)
{
    *count = 0;
    if (it->old < 0)
        return NULL;
    char **files = localdb_files(&ldb.pkgs[it->old], count);
    if (files)
        qsort(files, (size_t)*count, sizeof(char *), str_compare);
    return files;
}

static void run_hook(const struct pkg_info *info, const char *script, const char *old_version)
{
    if (!path_exists(script))
        return;
    setenv("PKG_ROOT", opt.root, 1);
    setenv("PKG_NAME", info->name, 1);
    setenv("PKG_VERSION", info->version, 1);
    setenv("PKG_OLD_VERSION", old_version ? old_version : "", 1);
    char *argv[] = {"/bin/sh", (char *)script, NULL};
    int rc = run_argv(argv);
    if (rc != 0)
        warn("%s: hook %s exited with status %d", info->name, strrchr(script, '/') + 1, rc);
}

static const struct pkg_backup *find_backup(const struct pkg_info *info, const char *path)
{
    for (int i = 0; i < info->backup_count; i++)
        if (!strcmp(info->backups[i].path, path))
            return &info->backups[i];
    return NULL;
}

static int extract_file(struct pkg_archive *a, const struct pkg_entry *e, const char *dest, char *hash_out)
{
    char tmp[PKG_PATH_MAX + 80];
    snprintf(tmp, sizeof(tmp), "%s.pkgtmp", dest);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return -1;
    char buf[8192];
    long got;
    int bad = 0;
    while ((got = pkg_archive_read(a, buf, sizeof(buf))) > 0)
        if (fwrite(buf, 1, (size_t)got, f) != (size_t)got) {
            bad = 1;
            break;
        }
    if (got < 0)
        bad = 1;
    if (fclose(f) != 0)
        bad = 1;
    if (bad) {
        unlink(tmp);
        return -1;
    }
    chmod(tmp, e->mode ? e->mode : 0644);
    if (hash_out && hash_file(tmp, hash_out) < 0)
        hash_out[0] = '\0';
    return rename(tmp, dest) < 0 ? (unlink(tmp), -1) : 0;
}

static int install_item(struct item *it)
{
    struct package_scan scan;
    if (scan_package(it->file, &scan) < 0) {
        warn("%s: cannot read the package %s", it->info.name, it->file);
        return -1;
    }
    if (strcmp(scan.info.name, it->info.name) != 0 || strcmp(scan.info.version, it->info.version) != 0) {
        warn("%s: the package contains %s-%s", it->info.name, scan.info.name, scan.info.version);
        scan_free(&scan);
        return -1;
    }
    struct pkg_info info = scan.info;
    info.csize = it->info.csize;
    snprintf(info.filename, sizeof(info.filename), "%s", it->info.filename);
    snprintf(info.sha512, sizeof(info.sha512), "%s", it->info.sha512);
    info.reason = it->reason;
    info.installdate = (long)time(NULL);

    int old_count;
    char **old_files = load_old_files(it, &old_count);
    struct pkg_info old_info;
    int have_old = it->old >= 0;
    if (have_old)
        old_info = ldb.pkgs[it->old];
    if (check_files(it, &scan, old_files, old_count) > 0) {
        free_list(old_files, old_count);
        scan_free(&scan);
        return -1;
    }

    char dbdir[PKG_PATH_MAX + 128];
    localdb_dir(&info, dbdir, sizeof(dbdir));
    char hookdir[PKG_PATH_MAX + 160];
    snprintf(hookdir, sizeof(hookdir), "%s/hooks", dbdir);
    mkdirs(hookdir, 0755);

    struct pkg_archive *a = pkg_archive_open(it->file);
    if (!a) {
        free_list(old_files, old_count);
        scan_free(&scan);
        return -1;
    }
    struct pkg_entry e;
    int rc, failed = 0;
    while ((rc = pkg_archive_next(a, &e)) == 1) {
        if (!strcmp(e.path, ".PKGINFO"))
            continue;
        if (is_meta(e.path)) {
            if (!strncmp(e.path, ".hook-", 6) && e.type == '0') {
                char target[PKG_PATH_MAX + 200];
                snprintf(target, sizeof(target), "%s/%s", hookdir, e.path + 6);
                if (extract_file(a, &e, target, NULL) == 0)
                    chmod(target, 0755);
            }
            continue;
        }
        const char *dest = rp(e.path);
        char parent[PKG_PATH_MAX + 64];
        snprintf(parent, sizeof(parent), "%s", dest);
        char *slash = strrchr(parent, '/');
        if (e.type != '5' && slash && slash != parent) {
            *slash = '\0';
            mkdirs(parent, 0755);
        }
        if (e.type == '5') {
            if (mkdirs(dest, e.mode ? e.mode : 0755) < 0)
                failed = 1;
        } else if (e.type == '2') {
            unlink(dest);
            if (symlink(e.link, dest) < 0) {
                warn("%s: cannot create the link %s: %s", info.name, e.path, strerror(errno));
                failed = 1;
            }
        } else if (e.type == '0') {
            struct pkg_backup *backup = NULL;
            for (int b = 0; b < info.backup_count; b++)
                if (!strcmp(info.backups[b].path, e.path))
                    backup = &info.backups[b];
            char new_hash[PKG_HASH_HEX] = "";
            if (!backup) {
                if (extract_file(a, &e, dest, NULL) < 0) {
                    warn("%s: cannot write %s: %s", info.name, e.path, strerror(errno));
                    failed = 1;
                }
            } else {
                char sidecar[PKG_PATH_MAX + 80];
                snprintf(sidecar, sizeof(sidecar), "%s.pkgnew", dest);
                char current[PKG_HASH_HEX] = "";
                int exists = path_exists(dest);
                if (exists)
                    hash_file(dest, current);
                const struct pkg_backup *previous = have_old ? find_backup(&old_info, e.path) : NULL;
                if (extract_file(a, &e, sidecar, new_hash) < 0) {
                    warn("%s: cannot write %s: %s", info.name, e.path, strerror(errno));
                    failed = 1;
                } else {
                    snprintf(backup->hash, sizeof(backup->hash), "%s", new_hash);
                    int keep_user = exists && previous && previous->hash[0] && strcmp(current, previous->hash) != 0;
                    if (keep_user && !strcmp(new_hash, previous->hash)) {
                        unlink(sidecar);
                    } else if (keep_user && !strcmp(current, new_hash)) {
                        unlink(sidecar);
                    } else if (keep_user) {
                        warn("%s: %s was modified, the new version is %s.pkgnew", info.name, e.path, e.path);
                    } else if (rename(sidecar, dest) < 0) {
                        warn("%s: cannot install %s: %s", info.name, e.path, strerror(errno));
                        unlink(sidecar);
                        failed = 1;
                    }
                }
            }
        }
        if (failed)
            break;
    }
    pkg_archive_close(a);
    if (rc < 0)
        failed = 1;
    if (failed) {
        warn("%s: installation failed, files may be partially written", info.name);
        free_list(old_files, old_count);
        scan_free(&scan);
        return -1;
    }

    if (have_old && old_files) {
        qsort(scan.files, (size_t)scan.file_count, sizeof(char *), str_compare);
        for (int i = old_count - 1; i >= 0; i--) {
            const char *path = old_files[i];
            if (list_has(scan.files, scan.file_count, path))
                continue;
            size_t n = strlen(path);
            char clean[PKG_PATH_MAX + 2];
            snprintf(clean, sizeof(clean), "%s", path);
            if (n && clean[n - 1] == '/') {
                clean[n - 1] = '\0';
                rmdir(rp(clean));
                continue;
            }
            const struct pkg_backup *b = find_backup(&old_info, clean);
            if (b && b->hash[0] && path_exists(rp(clean))) {
                char current[PKG_HASH_HEX];
                if (hash_file(rp(clean), current) == 0 && strcmp(current, b->hash) != 0) {
                    char saved[PKG_PATH_MAX + 80];
                    snprintf(saved, sizeof(saved), "%s.pkgsave", rp(clean));
                    rename(rp(clean), saved);
                    continue;
                }
            }
            unlink(rp(clean));
        }
    }

    if (localdb_write(&info, scan.files, scan.file_count) < 0) {
        warn("%s: cannot update the package database", info.name);
        free_list(old_files, old_count);
        scan_free(&scan);
        return -1;
    }
    if (have_old && strcmp(old_info.version, info.version) != 0)
        localdb_remove(&old_info);
    owners_invalidate();

    char script[PKG_PATH_MAX + 200];
    snprintf(script, sizeof(script), "%s/%s", hookdir, have_old ? "post-upgrade" : "post-install");
    run_hook(&info, script, have_old ? old_info.version : NULL);
    free_list(old_files, old_count);
    scan_free(&scan);
    return 0;
}

static void print_plan(const struct plan *plan, const char *verb)
{
    uint64_t download = 0, installed = 0;
    printf("\nPackages (%d):", plan->count);
    for (int i = 0; i < plan->count; i++) {
        const struct item *it = &plan->items[i];
        if (it->old >= 0 && strcmp(ldb.pkgs[it->old].version, it->info.version) != 0)
            printf(" %s %s->%s", it->info.name, ldb.pkgs[it->old].version, it->info.version);
        else
            printf(" %s-%s", it->info.name, it->info.version);
        if (it->sync_index >= 0)
            download += it->info.csize;
        installed += it->info.size;
    }
    char a[32], b[32];
    human_size(download, a, sizeof(a));
    human_size(installed, b, sizeof(b));
    printf("\n\nTotal Download Size:  %s\nTotal Installed Size: %s\n\n", a, b);
    (void)verb;
}

static int run_plan(struct plan *plan, const char *question)
{
    check_conflicts(plan);
    print_plan(plan, question);
    if (!confirm(question))
        return 0;
    int need_download = 0;
    for (int i = 0; i < plan->count; i++)
        if (plan->items[i].sync_index >= 0 && !plan->items[i].file[0])
            need_download = 1;
    if (need_download)
        msg("Retrieving packages...");
    for (int i = 0; i < plan->count; i++) {
        struct item *it = &plan->items[i];
        if (it->sync_index < 0 || it->file[0])
            continue;
        if (fetch_package(it->sync_index, it->file, sizeof(it->file)) < 0)
            die("failed to retrieve %s", it->info.name);
    }
    msg("Checking for file conflicts...");
    int conflicts = 0;
    for (int i = 0; i < plan->count; i++) {
        struct package_scan scan;
        struct item *it = &plan->items[i];
        if (scan_package(it->file, &scan) < 0)
            die("%s: cannot read the package %s", it->info.name, it->file);
        int old_count;
        char **old_files = load_old_files(it, &old_count);
        conflicts += check_files(it, &scan, old_files, old_count);
        free_list(old_files, old_count);
        scan_free(&scan);
    }
    if (conflicts)
        die("unresolved file conflicts, nothing was changed (--force overwrites files that belong to no package)");
    msg("Processing package changes...");
    for (int i = 0; i < plan->count; i++) {
        struct item *it = &plan->items[i];
        it->old = localdb_find(it->info.name);
        const char *what = it->old < 0 ? "installing" : (!strcmp(ldb.pkgs[it->old].version, it->info.version) ? "reinstalling" : "upgrading");
        if (it->old >= 0 && strcmp(ldb.pkgs[it->old].version, it->info.version) != 0)
            printf("(%d/%d) %s %s (%s -> %s)\n", i + 1, plan->count, what, it->info.name, ldb.pkgs[it->old].version,
                   it->info.version);
        else
            printf("(%d/%d) %s %s-%s\n", i + 1, plan->count, what, it->info.name, it->info.version);
        if (install_item(it) < 0)
            die("%s: failed, stopping", it->info.name);
        localdb_load();
    }
    return plan->count;
}

static void plan_free(struct plan *plan)
{
    free(plan->items);
    plan->items = NULL;
    plan->count = plan->cap = 0;
}

int tx_install(char **targets, int count, int local_files)
{
    struct plan plan = {0};
    int need_msg = 0;
    for (int i = 0; i < count; i++) {
        if (local_files) {
            struct package_scan scan;
            if (scan_package(targets[i], &scan) < 0)
                die("%s: not a valid package", targets[i]);
            struct stat st;
            if (stat(targets[i], &st) == 0)
                scan.info.csize = (uint64_t)st.st_size;
            add_package(&plan, &scan.info, -1, targets[i], opt.asdeps ? 1 : 0, NULL);
            scan_free(&scan);
            continue;
        }
        const char *name = targets[i];
        if (strstr(name, "://") || !strncmp(name, "github.com/", 11)) {
            char why[256];
            name = package_for_url(targets[i], why, sizeof(why));
            if (!name)
                die("%s", why);
        }
        int s = syncdb_find(name);
        if (s < 0)
            die("target not found: %s (no repository of the registry provides it; pkg -Sy, pkg -Sl)", targets[i]);
        int installed = localdb_find(name);
        if (installed >= 0 && opt.needed && pkg_vercmp(ldb.pkgs[installed].version, sdb.pkgs[s].version) >= 0) {
            note(" %s-%s is up to date -- skipping", name, ldb.pkgs[installed].version);
            continue;
        }
        if (installed >= 0 && !opt.needed && !strcmp(ldb.pkgs[installed].version, sdb.pkgs[s].version))
            warn("%s-%s is already installed, reinstalling", name, ldb.pkgs[installed].version);
        if (!need_msg++)
            msg("Retrieving packages...");
        sync_complete(s);
        add_package(&plan, &sdb.pkgs[s], s, NULL, opt.asdeps ? 1 : 0, NULL);
    }
    if (!plan.count) {
        note(" there is nothing to do");
        return 0;
    }
    int done = run_plan(&plan, "Proceed with installation?");
    plan_free(&plan);
    return done;
}

int list_upgrades(int quiet)
{
    int n = 0;
    for (int i = 0; i < ldb.count; i++) {
        int s = syncdb_find(ldb.pkgs[i].name);
        if (s < 0 || pkg_vercmp(sdb.pkgs[s].version, ldb.pkgs[i].version) <= 0)
            continue;
        if (quiet)
            printf("%s\n", ldb.pkgs[i].name);
        else
            printf("%s %s -> %s\n", ldb.pkgs[i].name, ldb.pkgs[i].version, sdb.pkgs[s].version);
        n++;
    }
    return n;
}

int tx_upgrade(int unused)
{
    (void)unused;
    struct plan plan = {0};
    int need_msg = 0;
    for (int i = 0; i < ldb.count; i++) {
        int s = syncdb_find(ldb.pkgs[i].name);
        if (s < 0 || pkg_vercmp(sdb.pkgs[s].version, ldb.pkgs[i].version) <= 0)
            continue;
        if (!need_msg++)
            msg("Retrieving packages...");
        sync_complete(s);
        if (pkg_vercmp(sdb.pkgs[s].version, ldb.pkgs[i].version) <= 0)
            continue;
        add_package(&plan, &sdb.pkgs[s], s, NULL, ldb.pkgs[i].reason, NULL);
    }
    if (!plan.count) {
        note(" there is nothing to do");
        return 0;
    }
    int done = run_plan(&plan, "Proceed with the upgrade?");
    plan_free(&plan);
    return done;
}

static int depends_on(const struct pkg_info *p, const char *name)
{
    for (int i = 0; i < p->depend_count; i++) {
        char n[PKG_NAME_MAX], op[8], v[PKG_VERSION_MAX];
        if (pkg_dep_split(p->depends[i], n, sizeof(n), op, sizeof(op), v, sizeof(v)) == 0 && !strcmp(n, name))
            return 1;
    }
    return 0;
}

static void remove_item(const struct pkg_info *p)
{
    char dbdir[PKG_PATH_MAX + 128];
    localdb_dir(p, dbdir, sizeof(dbdir));
    char script[PKG_PATH_MAX + 200];
    snprintf(script, sizeof(script), "%s/hooks/pre-remove", dbdir);
    run_hook(p, script, NULL);
    int count;
    char **files = localdb_files(p, &count);
    for (int i = count - 1; i >= 0; i--) {
        size_t n = strlen(files[i]);
        char clean[PKG_PATH_MAX + 2];
        snprintf(clean, sizeof(clean), "%s", files[i]);
        if (n && clean[n - 1] == '/') {
            clean[n - 1] = '\0';
            rmdir(rp(clean));
            continue;
        }
        const struct pkg_backup *b = find_backup(p, clean);
        if (b && b->hash[0] && path_exists(rp(clean))) {
            char current[PKG_HASH_HEX];
            if (hash_file(rp(clean), current) == 0 && strcmp(current, b->hash) != 0) {
                char saved[PKG_PATH_MAX + 80];
                snprintf(saved, sizeof(saved), "%s.pkgsave", rp(clean));
                rename(rp(clean), saved);
                warn("%s: %s was modified and is saved as %s.pkgsave", p->name, clean, clean);
                continue;
            }
        }
        if (unlink(rp(clean)) < 0 && errno != ENOENT)
            warn("%s: cannot remove %s: %s", p->name, clean, strerror(errno));
    }
    free_list(files, count);
    snprintf(script, sizeof(script), "%s/hooks/post-remove", dbdir);
    run_hook(p, script, NULL);
    localdb_remove(p);
}

int tx_remove(char **targets, int count, int recursive)
{
    int *marked = calloc((size_t)(ldb.count ? ldb.count : 1), sizeof(int));
    if (!marked)
        die("out of memory");
    for (int i = 0; i < count; i++) {
        int idx = localdb_find(targets[i]);
        if (idx < 0)
            die("target not found: %s", targets[i]);
        marked[idx] = 1;
    }
    if (recursive) {
        int changed = 1;
        while (changed) {
            changed = 0;
            for (int i = 0; i < ldb.count; i++) {
                if (marked[i] || ldb.pkgs[i].reason != 1)
                    continue;
                int needed = 0;
                for (int j = 0; j < ldb.count && !needed; j++)
                    if (!marked[j] && j != i && depends_on(&ldb.pkgs[j], ldb.pkgs[i].name))
                        needed = 1;
                if (!needed) {
                    marked[i] = 1;
                    changed = 1;
                }
            }
        }
    }
    if (!opt.nodeps)
        for (int i = 0; i < ldb.count; i++)
            if (marked[i])
                for (int j = 0; j < ldb.count; j++)
                    if (!marked[j] && depends_on(&ldb.pkgs[j], ldb.pkgs[i].name))
                        die("removing %s breaks the dependency of %s", ldb.pkgs[i].name, ldb.pkgs[j].name);
    int total = 0;
    uint64_t freed = 0;
    printf("\nPackages (");
    for (int i = 0; i < ldb.count; i++)
        total += marked[i];
    printf("%d):", total);
    for (int i = 0; i < ldb.count; i++)
        if (marked[i]) {
            printf(" %s-%s", ldb.pkgs[i].name, ldb.pkgs[i].version);
            freed += ldb.pkgs[i].size;
        }
    char sz[32];
    human_size(freed, sz, sizeof(sz));
    printf("\n\nTotal Removed Size: %s\n\n", sz);
    if (!confirm("Do you want to remove these packages?")) {
        free(marked);
        return 0;
    }
    struct pkg_info *copy = malloc((size_t)(total ? total : 1) * sizeof(*copy));
    int n = 0;
    for (int i = 0; i < ldb.count; i++)
        if (marked[i])
            copy[n++] = ldb.pkgs[i];
    for (int i = 0; i < n; i++) {
        printf("(%d/%d) removing %s-%s\n", i + 1, n, copy[i].name, copy[i].version);
        remove_item(&copy[i]);
    }
    free(copy);
    free(marked);
    owners_invalidate();
    return total;
}
