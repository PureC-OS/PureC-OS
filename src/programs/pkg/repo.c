#include "json.h"
#include "pkgd.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_SOURCES 128
#define MAX_PER_REPO 32
#define SIG_NEVER 0
#define SIG_OPTIONAL 1
#define SIG_REQUIRED 2
#define REGISTRY_FILE "registry.conf"

struct source {
    char url[PKG_PATH_MAX];
    char owner_repo[96];
    int local;
};

static struct source sources[MAX_SOURCES];
static int sources_total;
static int locals_ignored;
static char arch[16] = "x86_64";
static char api_base[PKG_PATH_MAX] = "https://api.github.com";
static char registry[PKG_PATH_MAX];
static int siglevel = SIG_OPTIONAL;
static int allow_prerelease;
static int allow_local;
static char config_used[PKG_PATH_MAX];

struct syncdb sdb;

int repo_count(void)
{
    return sources_total;
}

const char *config_path(void)
{
    return config_used;
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

static int yes(const char *v)
{
    return !strcasecmp(v, "yes") || !strcasecmp(v, "true") || !strcmp(v, "1");
}

static int parse_siglevel(const char *value)
{
    if (!strcasecmp(value, "Never"))
        return SIG_NEVER;
    if (!strcasecmp(value, "Required"))
        return SIG_REQUIRED;
    return SIG_OPTIONAL;
}

static int parse_github(const char *url, char *out, size_t cap)
{
    const char *p = url;
    if (!strncmp(p, "https://", 8))
        p += 8;
    else if (!strncmp(p, "http://", 7))
        p += 7;
    if (!strncmp(p, "www.", 4))
        p += 4;
    if (strncmp(p, "github.com/", 11) != 0)
        return -1;
    p += 11;
    const char *slash = strchr(p, '/');
    if (!slash || slash == p || !slash[1])
        return -1;
    size_t n = strlen(p);
    while (n && p[n - 1] == '/')
        n--;
    if (n > 4 && !strncmp(p + n - 4, ".git", 4))
        n -= 4;
    const char *extra = memchr(slash + 1, '/', n - (size_t)(slash + 1 - p));
    if (extra)
        n = (size_t)(extra - p);
    if (n + 1 > cap)
        return -1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

static int valid_name(const char *name)
{
    if (!*name || strlen(name) >= PKG_NAME_MAX)
        return 0;
    for (const char *p = name; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_' && *p != '.' && *p != '+')
            return 0;
    return 1;
}

static int add_source(const char *url, int local, const char *where, int line)
{
    char owner_repo[96];
    if (parse_github(url, owner_repo, sizeof(owner_repo)) < 0) {
        warn("%s:%d: '%s' is not a GitHub repository URL, ignored", where, line, url);
        return -1;
    }
    for (int i = 0; i < sources_total; i++)
        if (!strcasecmp(sources[i].owner_repo, owner_repo))
            return 0;
    if (sources_total == MAX_SOURCES) {
        warn("%s: too many repositories, %s ignored", where, url);
        return -1;
    }
    struct source *s = &sources[sources_total++];
    memset(s, 0, sizeof(*s));
    snprintf(s->url, sizeof(s->url), "https://github.com/%s", owner_repo);
    snprintf(s->owner_repo, sizeof(s->owner_repo), "%s", owner_repo);
    s->local = local;
    return 0;
}

static char *value_of_line(char *s)
{
    char *eq = strchr(s, '=');
    return eq ? strip(eq + 1) : s;
}

static void load_registry_file(const char *path)
{
    char *text = read_text_file(path, NULL);
    if (!text)
        return;
    int lineno = 0;
    for (char *line = text; line && *line;) {
        char *eol = strchr(line, '\n');
        if (eol)
            *eol = '\0';
        lineno++;
        char *s = strip(line);
        if (*s && *s != '#')
            add_source(value_of_line(s), 0, "registry", lineno);
        line = eol ? eol + 1 : NULL;
    }
    free(text);
}

static int load_file(const char *path, int required)
{
    char *text = read_text_file(path, NULL);
    if (!text) {
        if (required)
            die("cannot read %s", path);
        return -1;
    }
    snprintf(config_used, sizeof(config_used), "%s", path);
    enum { NONE, OPTIONS, TRUSTED } section = NONE;
    int lineno = 0;
    static char locals[MAX_SOURCES][PKG_PATH_MAX];
    int local_lines[MAX_SOURCES];
    int local_count = 0;
    char *line = text;
    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol)
            *eol = '\0';
        lineno++;
        char *s = strip(line);
        if (*s && *s != '#') {
            if (*s == '[') {
                char *end = strchr(s, ']');
                if (!end)
                    die("%s:%d: malformed section '%s'", path, lineno, s);
                *end = '\0';
                if (!strcmp(s + 1, "options"))
                    section = OPTIONS;
                else if (!strcmp(s + 1, "trusted"))
                    section = TRUSTED;
                else
                    die("%s:%d: unknown section [%s]", path, lineno, s + 1);
            } else if (section == OPTIONS) {
                char *eq = strchr(s, '=');
                if (!eq)
                    die("%s:%d: expected 'key = value'", path, lineno);
                *eq = '\0';
                char *key = strip(s);
                char *value = strip(eq + 1);
                if (!strcmp(key, "Architecture"))
                    snprintf(arch, sizeof(arch), "%s", value);
                else if (!strcmp(key, "SigLevel"))
                    siglevel = parse_siglevel(value);
                else if (!strcmp(key, "API"))
                    snprintf(api_base, sizeof(api_base), "%s", value);
                else if (!strcmp(key, "Prerelease"))
                    allow_prerelease = yes(value);
                else if (!strcmp(key, "Registry"))
                    snprintf(registry, sizeof(registry), "%s", value);
                else if (!strcmp(key, "AllowLocalRepos"))
                    allow_local = yes(value);
            } else if (section == TRUSTED) {
                if (local_count < MAX_SOURCES) {
                    snprintf(locals[local_count], PKG_PATH_MAX, "%s", value_of_line(s));
                    local_lines[local_count++] = lineno;
                }
            } else {
                die("%s:%d: setting outside of a section", path, lineno);
            }
        }
        if (!eol)
            break;
        line = eol + 1;
    }
    free(text);
    size_t n = strlen(api_base);
    while (n > 1 && api_base[n - 1] == '/')
        api_base[--n] = '\0';
    char cache[PKG_PATH_MAX + 32];
    snprintf(cache, sizeof(cache), "%s/" REGISTRY_FILE, rp("var/lib/pkg/sync"));
    if (registry[0])
        load_registry_file(cache);
    locals_ignored = 0;
    for (int i = 0; i < local_count; i++) {
        if (allow_local)
            add_source(locals[i], 1, path, local_lines[i]);
        else
            locals_ignored++;
    }
    return 0;
}

void config_load(void)
{
    sources_total = 0;
    config_used[0] = '\0';
    registry[0] = '\0';
    allow_local = 0;
    if (opt.config) {
        load_file(opt.config, 1);
        return;
    }
    if (load_file(rp("etc/pkg/pkg.conf"), 0) == 0)
        return;
    load_file(rp("etc/pkg.conf"), 0);
}

static int curl_get(const char *url, const char *dest, int api, int quiet)
{
    if (!strncmp(url, "file://", 7) || url[0] == '/')
        return copy_file(url[0] == '/' ? url : url + 7, dest);
    char tmp[PKG_PATH_MAX + 16];
    snprintf(tmp, sizeof(tmp), "%s.part", dest);
    unlink(tmp);
    char *argv[24];
    int n = 0;
    argv[n++] = "curl";
    argv[n++] = "-fL";
    argv[n++] = "--retry";
    argv[n++] = "2";
    argv[n++] = "--connect-timeout";
    argv[n++] = "20";
    argv[n++] = "--speed-limit";
    argv[n++] = "1024";
    argv[n++] = "--speed-time";
    argv[n++] = "60";
    argv[n++] = (!quiet && isatty(1)) ? "-#" : "-sS";
    if (api) {
        argv[n++] = "-H";
        argv[n++] = "Accept: application/vnd.github+json";
        argv[n++] = "-H";
        argv[n++] = "X-GitHub-Api-Version: 2022-11-28";
    }
    argv[n++] = "-o";
    argv[n++] = tmp;
    argv[n++] = (char *)url;
    argv[n] = NULL;
    if (run_argv(argv) != 0 || rename(tmp, dest) < 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && !strcmp(s + a - b, suffix);
}

static int parse_asset(const char *asset, char *name, size_t ncap, char *version, size_t vcap)
{
    char suffix[40];
    const char *arches[2] = {arch, "any"};
    size_t len = 0;
    for (int i = 0; i < 2 && !len; i++) {
        snprintf(suffix, sizeof(suffix), "-%s.pkg.tar.gz", arches[i]);
        if (ends_with(asset, suffix))
            len = strlen(asset) - strlen(suffix);
    }
    if (!len)
        return -1;
    char base[PKG_FILENAME_MAX];
    if (len + 1 > sizeof(base))
        return -1;
    memcpy(base, asset, len);
    base[len] = '\0';
    char *rel = strrchr(base, '-');
    if (!rel || rel == base || !rel[1])
        return -1;
    *rel = '\0';
    char *ver = strrchr(base, '-');
    if (!ver || ver == base || !isdigit((unsigned char)ver[1]))
        return -1;
    *ver = '\0';
    if (!valid_name(base) || strlen(base) + 1 > ncap)
        return -1;
    snprintf(name, ncap, "%s", base);
    if ((size_t)snprintf(version, vcap, "%s-%s", ver + 1, rel + 1) >= vcap)
        return -1;
    return 0;
}

struct found {
    char name[PKG_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char tag[64];
    char asset[PKG_FILENAME_MAX];
    char url[PKG_PATH_MAX];
    char sig[PKG_PATH_MAX];
    char desc[PKG_DESC_MAX];
    uint64_t size;
};

static void scan_release(const char *release, struct found *table, int *count)
{
    const char *v;
    if ((v = json_get(release, "draft")) && !strncmp(v, "true", 4))
        return;
    if (!allow_prerelease && (v = json_get(release, "prerelease")) && !strncmp(v, "true", 4))
        return;
    const char *assets = json_get(release, "assets");
    if (!assets || *assets != '[')
        return;
    char tag[64] = "", relname[PKG_DESC_MAX] = "";
    if ((v = json_get(release, "tag_name")))
        json_string(v, tag, sizeof(tag));
    if ((v = json_get(release, "name")))
        json_string(v, relname, sizeof(relname));
    const char *cursor = assets, *asset;
    while (json_array_next(&cursor, &asset) == 1) {
        char aname[PKG_FILENAME_MAX], name[PKG_NAME_MAX], version[PKG_VERSION_MAX];
        const char *nv = json_get(asset, "name");
        if (!nv || json_string(nv, aname, sizeof(aname)) < 0)
            continue;
        if (parse_asset(aname, name, sizeof(name), version, sizeof(version)) < 0)
            continue;
        int slot = -1;
        for (int i = 0; i < *count; i++)
            if (!strcmp(table[i].name, name))
                slot = i;
        if (slot >= 0 && pkg_vercmp(version, table[slot].version) <= 0)
            continue;
        if (slot < 0) {
            if (*count == MAX_PER_REPO)
                continue;
            slot = (*count)++;
        }
        struct found *f = &table[slot];
        memset(f, 0, sizeof(*f));
        snprintf(f->name, sizeof(f->name), "%s", name);
        snprintf(f->version, sizeof(f->version), "%s", version);
        snprintf(f->tag, sizeof(f->tag), "%s", tag);
        snprintf(f->asset, sizeof(f->asset), "%s", aname);
        snprintf(f->desc, sizeof(f->desc), "%s", relname);
        const char *uv = json_get(asset, "browser_download_url");
        if (uv)
            json_string(uv, f->url, sizeof(f->url));
        const char *sv = json_get(asset, "size");
        f->size = sv ? (uint64_t)json_number(sv) : 0;
        char signame[PKG_FILENAME_MAX + 8];
        snprintf(signame, sizeof(signame), "%s.sig", aname);
        const char *c2 = assets, *a2;
        while (json_array_next(&c2, &a2) == 1) {
            char n2[PKG_FILENAME_MAX + 8];
            const char *nv2 = json_get(a2, "name");
            if (!nv2 || json_string(nv2, n2, sizeof(n2)) < 0 || strcmp(n2, signame) != 0)
                continue;
            const char *u2 = json_get(a2, "browser_download_url");
            if (u2)
                json_string(u2, f->sig, sizeof(f->sig));
        }
    }
}

static int query_source(const struct source *src, struct found *table, int *count, char *why, size_t why_cap)
{
    *count = 0;
    char url[PKG_PATH_MAX + 128], dest[PKG_PATH_MAX];
    snprintf(dest, sizeof(dest), "%s/releases.json", rp("var/lib/pkg/sync"));
    snprintf(url, sizeof(url), "%s/repos/%s/releases?per_page=30", api_base, src->owner_repo);
    if (curl_get(url, dest, 1, 1) < 0) {
        snprintf(why, why_cap, "cannot reach %s", src->url);
        return -1;
    }
    char *text = read_text_file(dest, NULL);
    unlink(dest);
    if (!text) {
        snprintf(why, why_cap, "empty answer");
        return -1;
    }
    const char *p = json_ws(text);
    int releases = 0;
    if (p && *p == '[') {
        const char *cursor = p, *release;
        while (json_array_next(&cursor, &release) == 1) {
            releases++;
            scan_release(release, table, count);
        }
    } else {
        char message[200] = "unexpected answer";
        const char *m = json_get(p, "message");
        if (m)
            json_string(m, message, sizeof(message));
        snprintf(why, why_cap, "GitHub: %s", message);
        free(text);
        return -1;
    }
    free(text);
    if (!*count) {
        if (!releases)
            snprintf(why, why_cap, "no releases");
        else
            snprintf(why, why_cap, "no NAME-VERSION-REL-%s.pkg.tar.gz in the releases", arch);
        return 1;
    }
    return 0;
}

static void src_path(const char *name, char *out, size_t cap)
{
    snprintf(out, cap, "%s/%s.src", rp("var/lib/pkg/sync"), name);
}

static void write_src(const struct source *src, const struct found *f)
{
    char path[PKG_PATH_MAX + 80], body[4096];
    src_path(f->name, path, sizeof(path));
    int n = snprintf(body, sizeof(body),
                     "name=%s\nversion=%s\nrepo=%s\norigin=%s\ntag=%s\nasset=%s\nurl=%s\nsig=%s\nsize=%llu\n"
                     "desc=%s\n",
                     f->name, f->version, src->owner_repo, src->local ? "local" : "registry", f->tag, f->asset,
                     f->url, f->sig, (unsigned long long)f->size, f->desc);
    if (n > 0 && (size_t)n < sizeof(body))
        write_text_file(path, body, (size_t)n);
}

static int src_field(const char *path, const char *key, char *out, size_t cap)
{
    char *text = read_text_file(path, NULL);
    if (!text)
        return -1;
    size_t kl = strlen(key);
    int found = -1;
    for (char *line = text; line && *line;) {
        char *eol = strchr(line, '\n');
        if (eol)
            *eol = '\0';
        if (!strncmp(line, key, kl) && line[kl] == '=') {
            snprintf(out, cap, "%s", line + kl + 1);
            found = 0;
            break;
        }
        line = eol ? eol + 1 : NULL;
    }
    free(text);
    return found;
}

static int fetch_registry(void)
{
    if (!registry[0])
        return 0;
    char url[PKG_PATH_MAX + 64], owner_repo[96];
    if (!strncmp(registry, "file://", 7) || registry[0] == '/' || ends_with(registry, ".conf"))
        snprintf(url, sizeof(url), "%s", registry);
    else if (parse_github(registry, owner_repo, sizeof(owner_repo)) == 0)
        snprintf(url, sizeof(url), "https://raw.githubusercontent.com/%s/HEAD/" REGISTRY_FILE, owner_repo);
    else
        die("Registry = %s is neither a GitHub repository nor a .conf address", registry);
    char dest[PKG_PATH_MAX + 32], fresh[PKG_PATH_MAX + 40], sig[PKG_PATH_MAX + 48], sigurl[PKG_PATH_MAX + 72];
    snprintf(dest, sizeof(dest), "%s/" REGISTRY_FILE, rp("var/lib/pkg/sync"));
    snprintf(fresh, sizeof(fresh), "%s.new", dest);
    snprintf(sig, sizeof(sig), "%s.sig", fresh);
    snprintf(sigurl, sizeof(sigurl), "%s.sig", url);
    printf(" %-36s ", "registry");
    fflush(stdout);
    if (curl_get(url, fresh, 0, 1) < 0) {
        printf("failed\n");
        warn("cannot download the registry %s, using the last copy", url);
        return -1;
    }
    if (siglevel != SIG_NEVER) {
        int have_sig = curl_get(sigurl, sig, 0, 1) == 0;
        if (have_sig) {
            int rc = sig_verify_file(fresh, sig);
            unlink(sig);
            if (rc == SIG_BAD || (rc == SIG_NOKEYS && siglevel == SIG_REQUIRED)) {
                printf("failed\n");
                warn("the registry signature is %s, keeping the last copy",
                     rc == SIG_BAD ? "invalid" : "not checkable (no keys)");
                unlink(fresh);
                return -1;
            }
        } else if (siglevel == SIG_REQUIRED) {
            printf("failed\n");
            warn("the registry has no signature, SigLevel = Required");
            unlink(fresh);
            return -1;
        }
    }
    rename(fresh, dest);
    printf("done\n");
    return 0;
}

int sync_refresh(void)
{
    mkdirs(rp("var/lib/pkg/sync"), 0755);
    msg("Checking the registry and its repositories...");
    int failed = fetch_registry() < 0;
    char keep_config[PKG_PATH_MAX];
    snprintf(keep_config, sizeof(keep_config), "%s", config_used);
    config_load();
    if (!sources_total)
        die("no repositories: set Registry in %s", keep_config[0] ? keep_config : rp("etc/pkg/pkg.conf"));
    if (locals_ignored)
        warn("%d repositories in [trusted] are ignored, set AllowLocalRepos = yes to use them", locals_ignored);
    static char written[MAX_SOURCES * 4][PKG_NAME_MAX];
    int written_count = 0;
    int ok_repo[MAX_SOURCES] = {0};
    for (int i = 0; i < sources_total; i++) {
        const struct source *src = &sources[i];
        struct found table[MAX_PER_REPO];
        int count = 0;
        char why[300];
        printf(" %-36s ", src->owner_repo);
        fflush(stdout);
        int rc = query_source(src, table, &count, why, sizeof(why));
        if (rc != 0) {
            printf("%s\n", why);
            if (rc < 0)
                failed++;
            else
                ok_repo[i] = 1;
            continue;
        }
        ok_repo[i] = 1;
        int shown = 0;
        for (int k = 0; k < count; k++) {
            int dup = 0;
            for (int w = 0; w < written_count; w++)
                if (!strcmp(written[w], table[k].name))
                    dup = 1;
            if (dup) {
                printf("%s", shown ? "" : "\n");
                warn("%s: package %s is already provided by an earlier repository, skipped", src->owner_repo,
                     table[k].name);
                continue;
            }
            write_src(src, &table[k]);
            if (written_count < (int)(sizeof(written) / sizeof(written[0])))
                snprintf(written[written_count++], PKG_NAME_MAX, "%s", table[k].name);
            printf("%s%s %s", shown++ ? ", " : "", table[k].name, table[k].version);
        }
        printf("%s\n", src->local ? "  (local)" : "");
    }
    DIR *d = opendir(rp("var/lib/pkg/sync"));
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            size_t n = strlen(e->d_name);
            if (n <= 4 || strcmp(e->d_name + n - 4, ".src") != 0)
                continue;
            char name[PKG_NAME_MAX], path[PKG_PATH_MAX + 300], repo[96] = "";
            snprintf(name, sizeof(name), "%.*s", (int)(n - 4), e->d_name);
            snprintf(path, sizeof(path), "%s/%s", rp("var/lib/pkg/sync"), e->d_name);
            int keep = 0;
            for (int w = 0; w < written_count; w++)
                if (!strcmp(written[w], name))
                    keep = 1;
            if (!keep && src_field(path, "repo", repo, sizeof(repo)) == 0) {
                for (int i = 0; i < sources_total; i++)
                    if (!strcasecmp(sources[i].owner_repo, repo) && !ok_repo[i])
                        keep = 1;
            }
            if (!keep)
                unlink(path);
        }
        closedir(d);
    }
    return failed ? -1 : 0;
}

static void sync_add(const struct pkg_info *info, const struct sync_meta *meta)
{
    if (sdb.count == sdb.cap) {
        int cap = sdb.cap ? sdb.cap * 2 : 64;
        sdb.pkgs = realloc(sdb.pkgs, (size_t)cap * sizeof(*sdb.pkgs));
        sdb.meta = realloc(sdb.meta, (size_t)cap * sizeof(*sdb.meta));
        if (!sdb.pkgs || !sdb.meta)
            die("out of memory");
        sdb.cap = cap;
    }
    sdb.pkgs[sdb.count] = *info;
    sdb.meta[sdb.count] = *meta;
    sdb.count++;
}

static int source_allowed(const char *owner_repo, int *local)
{
    for (int i = 0; i < sources_total; i++)
        if (!strcasecmp(sources[i].owner_repo, owner_repo)) {
            *local = sources[i].local;
            return 1;
        }
    return 0;
}

void syncdb_load(void)
{
    free(sdb.pkgs);
    free(sdb.meta);
    memset(&sdb, 0, sizeof(sdb));
    DIR *d = opendir(rp("var/lib/pkg/sync"));
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        if (len <= 4 || strcmp(e->d_name + len - 4, ".src") != 0)
            continue;
        char path[PKG_PATH_MAX + 300];
        snprintf(path, sizeof(path), "%s/%s", rp("var/lib/pkg/sync"), e->d_name);
        char *text = read_text_file(path, NULL);
        if (!text)
            continue;
        struct pkg_info info;
        struct sync_meta meta;
        memset(&info, 0, sizeof(info));
        memset(&meta, 0, sizeof(meta));
        for (char *line = text; line && *line;) {
            char *eol = strchr(line, '\n');
            if (eol)
                *eol = '\0';
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                const char *k = line, *v = eq + 1;
                if (!strcmp(k, "name"))
                    snprintf(info.name, sizeof(info.name), "%s", v);
                else if (!strcmp(k, "version"))
                    snprintf(info.version, sizeof(info.version), "%s", v);
                else if (!strcmp(k, "repo"))
                    snprintf(meta.repo, sizeof(meta.repo), "%s", v);
                else if (!strcmp(k, "tag"))
                    snprintf(meta.tag, sizeof(meta.tag), "%s", v);
                else if (!strcmp(k, "asset"))
                    snprintf(info.filename, sizeof(info.filename), "%s", v);
                else if (!strcmp(k, "url"))
                    snprintf(meta.url, sizeof(meta.url), "%s", v);
                else if (!strcmp(k, "sig"))
                    snprintf(meta.sig, sizeof(meta.sig), "%s", v);
                else if (!strcmp(k, "size"))
                    info.csize = strtoull(v, NULL, 10);
                else if (!strcmp(k, "desc"))
                    snprintf(info.desc, sizeof(info.desc), "%s", v);
            }
            line = eol ? eol + 1 : NULL;
        }
        free(text);
        int local = 0;
        if (!info.name[0] || !info.version[0] || !meta.url[0] || !info.filename[0] ||
            !source_allowed(meta.repo, &local))
            continue;
        meta.local = local;
        snprintf(info.arch, sizeof(info.arch), "%s", arch);
        sync_add(&info, &meta);
    }
    closedir(d);
}

int syncdb_find(const char *name)
{
    for (int i = 0; i < sdb.count; i++)
        if (!strcmp(sdb.pkgs[i].name, name))
            return i;
    return -1;
}

const char *package_for_url(const char *url, char *why, size_t cap)
{
    char owner_repo[96];
    if (parse_github(url, owner_repo, sizeof(owner_repo)) < 0) {
        snprintf(why, cap, "%s is not a GitHub repository URL", url);
        return NULL;
    }
    int local = 0;
    if (!source_allowed(owner_repo, &local)) {
        snprintf(why, cap, "%s is not in the registry%s", owner_repo, allow_local ? " or in [trusted]" : "");
        return NULL;
    }
    const char *found = NULL;
    int count = 0;
    for (int i = 0; i < sdb.count; i++)
        if (!strcasecmp(sdb.meta[i].repo, owner_repo)) {
            found = sdb.pkgs[i].name;
            count++;
        }
    if (count == 1)
        return found;
    if (!count)
        snprintf(why, cap, "%s has no packages (run 'pkg -Sy')", owner_repo);
    else
        snprintf(why, cap, "%s provides %d packages, name one of them (pkg -Sl)", owner_repo, count);
    return NULL;
}

static int check_signature(const char *file, const struct sync_meta *m, int quiet)
{
    if (siglevel == SIG_NEVER)
        return 0;
    if (!m->sig[0]) {
        if (siglevel == SIG_REQUIRED) {
            if (!quiet)
                warn("%s: the release has no signature, SigLevel = Required", file);
            return -1;
        }
        return 0;
    }
    char sigfile[PKG_PATH_MAX + 8];
    snprintf(sigfile, sizeof(sigfile), "%s.sig", file);
    if (!path_exists(sigfile) && curl_get(m->sig, sigfile, 0, 1) < 0) {
        if (!quiet)
            warn("cannot download the signature %s", m->sig);
        return siglevel == SIG_REQUIRED ? -1 : 0;
    }
    int rc = sig_verify_file(file, sigfile);
    if (rc == SIG_OK)
        return 0;
    if (rc == SIG_NOKEYS && siglevel != SIG_REQUIRED) {
        if (!quiet)
            warn("signature not checked, no keys in %s", rp("etc/pkg/keys"));
        return 0;
    }
    if (!quiet)
        warn("%s: %s", file, rc == SIG_BAD ? "signature is invalid" : "no trusted key to check the signature");
    unlink(sigfile);
    return -1;
}

int fetch_package(int index, char *out_path, size_t cap)
{
    const struct pkg_info *p = &sdb.pkgs[index];
    const struct sync_meta *m = &sdb.meta[index];
    if (!p->filename[0] || strchr(p->filename, '/'))
        return -1;
    mkdirs(rp("var/cache/pkg"), 0755);
    snprintf(out_path, cap, "%s/%s", rp("var/cache/pkg"), p->filename);
    struct stat st;
    if (stat(out_path, &st) == 0 && (!p->csize || (uint64_t)st.st_size == p->csize) &&
        check_signature(out_path, m, 1) == 0)
        return 0;
    unlink(out_path);
    char sigfile[PKG_PATH_MAX + 8];
    snprintf(sigfile, sizeof(sigfile), "%s.sig", out_path);
    unlink(sigfile);
    printf(" %s (%s%s)\n", p->filename, m->repo, m->local ? ", local" : "");
    if (curl_get(m->url, out_path, 0, 0) < 0) {
        warn("download failed: %s", m->url);
        return -1;
    }
    if (p->csize && stat(out_path, &st) == 0 && (uint64_t)st.st_size != p->csize) {
        warn("%s: size does not match the release", p->filename);
        unlink(out_path);
        return -1;
    }
    if (check_signature(out_path, m, 0) < 0) {
        unlink(out_path);
        return -1;
    }
    return 0;
}

void list_sources(void)
{
    printf("Registry: %s\n", registry[0] ? registry : "(none)");
    for (int i = 0; i < sources_total; i++) {
        printf("%s%s\n", sources[i].url, sources[i].local ? "  (local)" : "");
        for (int k = 0; k < sdb.count; k++)
            if (!strcasecmp(sdb.meta[k].repo, sources[i].owner_repo))
                printf("    %-24s %s\n", sdb.pkgs[k].name, sdb.pkgs[k].version);
    }
    if (locals_ignored)
        printf("(%d repositories in [trusted] ignored: AllowLocalRepos is off)\n", locals_ignored);
}
