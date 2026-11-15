#include "pkgd.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_REPOS 8
#define MAX_SERVERS 6
#define SIG_NEVER 0
#define SIG_OPTIONAL 1
#define SIG_REQUIRED 2

struct repo {
    char name[32];
    char servers[MAX_SERVERS][PKG_PATH_MAX];
    int server_count;
    int siglevel;
};

static struct repo repos[MAX_REPOS];
static int repos_total;
static char arch[16] = "x86_64";
static int default_siglevel = SIG_OPTIONAL;

struct syncdb sdb;

int repo_count(void)
{
    return repos_total;
}

static char *strip(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
    return s;
}

static int parse_siglevel(const char *value)
{
    if (!strcasecmp(value, "Never"))
        return SIG_NEVER;
    if (!strcasecmp(value, "Required"))
        return SIG_REQUIRED;
    return SIG_OPTIONAL;
}

void config_load(void)
{
    repos_total = 0;
    const char *path = opt.config ? opt.config : rp("etc/pkg.conf");
    char *text = read_text_file(path, NULL);
    if (!text) {
        if (opt.config)
            die("cannot read %s", path);
        return;
    }
    struct repo *current = NULL;
    int in_options = 0;
    char *line = text;
    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol)
            *eol = '\0';
        char *s = strip(line);
        if (*s && *s != '#') {
            if (*s == '[') {
                char *end = strchr(s, ']');
                if (!end)
                    die("%s: malformed section '%s'", path, s);
                *end = '\0';
                const char *name = s + 1;
                if (!strcmp(name, "options")) {
                    in_options = 1;
                    current = NULL;
                } else {
                    in_options = 0;
                    if (repos_total == MAX_REPOS)
                        die("%s: too many repositories", path);
                    current = &repos[repos_total++];
                    memset(current, 0, sizeof(*current));
                    snprintf(current->name, sizeof(current->name), "%s", name);
                    current->siglevel = -1;
                }
            } else {
                char *eq = strchr(s, '=');
                if (eq) {
                    *eq = '\0';
                    char *key = strip(s);
                    char *value = strip(eq + 1);
                    if (in_options) {
                        if (!strcmp(key, "Architecture"))
                            snprintf(arch, sizeof(arch), "%s", value);
                        else if (!strcmp(key, "SigLevel"))
                            default_siglevel = parse_siglevel(value);
                    } else if (current) {
                        if (!strcmp(key, "Server") && current->server_count < MAX_SERVERS)
                            snprintf(current->servers[current->server_count++], PKG_PATH_MAX, "%s", value);
                        else if (!strcmp(key, "SigLevel"))
                            current->siglevel = parse_siglevel(value);
                    }
                }
            }
        }
        if (!eol)
            break;
        line = eol + 1;
    }
    free(text);
    for (int i = 0; i < repos_total; i++)
        if (repos[i].siglevel < 0)
            repos[i].siglevel = default_siglevel;
}

static void expand_server(const struct repo *r, const char *server, char *out, size_t cap)
{
    size_t pos = 0;
    for (const char *p = server; *p && pos + 1 < cap;) {
        if (!strncmp(p, "$repo", 5)) {
            pos += (size_t)snprintf(out + pos, cap - pos, "%s", r->name);
            p += 5;
        } else if (!strncmp(p, "$arch", 5)) {
            pos += (size_t)snprintf(out + pos, cap - pos, "%s", arch);
            p += 5;
        } else {
            out[pos++] = *p++;
        }
    }
    out[pos < cap ? pos : cap - 1] = '\0';
    size_t n = strlen(out);
    while (n > 1 && out[n - 1] == '/')
        out[--n] = '\0';
}

static int fetch_url(const char *base, const char *file, const char *dest)
{
    char url[PKG_PATH_MAX + 256];
    snprintf(url, sizeof(url), "%s/%s", base, file);
    if (!strncmp(url, "file://", 7) || url[0] == '/') {
        const char *local = url[0] == '/' ? url : url + 7;
        return copy_file(local, dest);
    }
    char tmp[PKG_PATH_MAX + 300];
    snprintf(tmp, sizeof(tmp), "%s.part", dest);
    unlink(tmp);
    char *argv[] = {"curl", "-fL", "--retry", "2", "-sS", "-o", tmp, url, NULL};
    char *argv_bar[] = {"curl", "-fL", "--retry", "2", "-#", "-o", tmp, url, NULL};
    int rc = run_argv(isatty(1) ? argv_bar : argv);
    if (rc != 0) {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, dest) < 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static int fetch_from_repo(const struct repo *r, const char *file, const char *dest)
{
    for (int i = 0; i < r->server_count; i++) {
        char base[PKG_PATH_MAX];
        expand_server(r, r->servers[i], base, sizeof(base));
        if (fetch_url(base, file, dest) == 0)
            return 0;
    }
    return -1;
}

int sync_refresh(void)
{
    if (!repos_total)
        die("no repositories configured in %s", rp("etc/pkg.conf"));
    msg("Synchronizing package databases...");
    mkdirs(rp("var/lib/pkg/sync"), 0755);
    int failed = 0;
    for (int i = 0; i < repos_total; i++) {
        struct repo *r = &repos[i];
        char dbfile[PKG_PATH_MAX], dbsig[PKG_PATH_MAX + 8], name[80];
        snprintf(dbfile, sizeof(dbfile), "%s/%s.db", rp("var/lib/pkg/sync"), r->name);
        snprintf(dbsig, sizeof(dbsig), "%s.sig", dbfile);
        snprintf(name, sizeof(name), "%s.db", r->name);
        char fresh[PKG_PATH_MAX + 8];
        snprintf(fresh, sizeof(fresh), "%s.new", dbfile);
        printf(" %s ... ", r->name);
        fflush(stdout);
        if (fetch_from_repo(r, name, fresh) < 0) {
            printf("failed\n");
            warn("failed to download %s", name);
            failed++;
            continue;
        }
        char sigfresh[PKG_PATH_MAX + 16];
        snprintf(sigfresh, sizeof(sigfresh), "%s.new", dbsig);
        int have_sig = 0;
        if (r->siglevel != SIG_NEVER) {
            char signame[96];
            snprintf(signame, sizeof(signame), "%s.db.sig", r->name);
            have_sig = fetch_from_repo(r, signame, sigfresh) == 0;
            if (!have_sig && r->siglevel == SIG_REQUIRED) {
                printf("failed\n");
                warn("%s: signature is required but missing", r->name);
                unlink(fresh);
                failed++;
                continue;
            }
        }
        if (have_sig) {
            int rc = sig_verify_file(fresh, sigfresh);
            if (rc == SIG_BAD || (rc == SIG_NOKEYS && r->siglevel == SIG_REQUIRED)) {
                printf("failed\n");
                warn("%s: %s", r->name, rc == SIG_BAD ? "signature is invalid" : "no trusted key to check the signature");
                unlink(fresh);
                unlink(sigfresh);
                failed++;
                continue;
            }
            if (rc == SIG_NOKEYS)
                warn("%s: signature not checked, no keys in %s", r->name, rp("etc/pkg/keys"));
            rename(sigfresh, dbsig);
        } else {
            unlink(dbsig);
        }
        if (rename(fresh, dbfile) < 0) {
            printf("failed\n");
            warn("cannot store %s", dbfile);
            failed++;
            continue;
        }
        printf("done\n");
    }
    return failed ? -1 : 0;
}

static void sync_add(const struct pkg_info *info, const char *repo)
{
    for (int i = 0; i < sdb.count; i++)
        if (!strcmp(sdb.pkgs[i].name, info->name))
            return;
    if (sdb.count == sdb.cap) {
        int cap = sdb.cap ? sdb.cap * 2 : 128;
        sdb.pkgs = realloc(sdb.pkgs, (size_t)cap * sizeof(*sdb.pkgs));
        sdb.repo = realloc(sdb.repo, (size_t)cap * sizeof(*sdb.repo));
        if (!sdb.pkgs || !sdb.repo)
            die("out of memory");
        sdb.cap = cap;
    }
    sdb.pkgs[sdb.count] = *info;
    snprintf(sdb.repo[sdb.count], sizeof(sdb.repo[0]), "%s", repo);
    sdb.count++;
}

void syncdb_load(void)
{
    free(sdb.pkgs);
    free(sdb.repo);
    memset(&sdb, 0, sizeof(sdb));
    for (int i = 0; i < repos_total; i++) {
        char path[PKG_PATH_MAX + 64];
        snprintf(path, sizeof(path), "%s/%s.db", rp("var/lib/pkg/sync"), repos[i].name);
        struct pkg_archive *a = pkg_archive_open(path);
        if (!a)
            continue;
        struct pkg_entry e;
        while (pkg_archive_next(a, &e) == 1) {
            size_t n = strlen(e.path);
            if (e.type != '0' || n < 5 || strcmp(e.path + n - 5, "/desc") != 0 || e.size > 16384)
                continue;
            char text[16385];
            long got = pkg_archive_read(a, text, e.size);
            if (got < 0 || (uint64_t)got != e.size)
                continue;
            text[got] = '\0';
            struct pkg_info info;
            if (pkg_info_parse(text, &info) == 0)
                sync_add(&info, repos[i].name);
        }
        pkg_archive_close(a);
    }
}

int syncdb_find(const char *name)
{
    for (int i = 0; i < sdb.count; i++)
        if (!strcmp(sdb.pkgs[i].name, name))
            return i;
    return -1;
}

int fetch_package(int index, char *out_path, size_t cap)
{
    const struct pkg_info *p = &sdb.pkgs[index];
    if (!p->filename[0])
        return -1;
    mkdirs(rp("var/cache/pkg"), 0755);
    snprintf(out_path, cap, "%s/%s", rp("var/cache/pkg"), p->filename);
    char hash[PKG_HASH_HEX];
    if (path_exists(out_path) && hash_file(out_path, hash) == 0 && !strcmp(hash, p->sha512))
        return 0;
    const struct repo *r = NULL;
    for (int i = 0; i < repos_total; i++)
        if (!strcmp(repos[i].name, sdb.repo[index]))
            r = &repos[i];
    if (!r)
        return -1;
    printf(" %s ... ", p->filename);
    fflush(stdout);
    if (fetch_from_repo(r, p->filename, out_path) < 0) {
        printf("failed\n");
        return -1;
    }
    if (hash_file(out_path, hash) < 0 || strcmp(hash, p->sha512) != 0) {
        printf("corrupted\n");
        unlink(out_path);
        return -1;
    }
    printf("done\n");
    return 0;
}
