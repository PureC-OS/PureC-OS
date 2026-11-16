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
#define SIG_NEVER 0
#define SIG_OPTIONAL 1
#define SIG_REQUIRED 2

struct source {
    char name[PKG_NAME_MAX];
    char url[PKG_PATH_MAX];
    char owner_repo[96];
};

static struct source sources[MAX_SOURCES];
static int sources_total;
static char arch[16] = "x86_64";
static char api_base[PKG_PATH_MAX] = "https://api.github.com";
static int siglevel = SIG_OPTIONAL;
static int allow_prerelease;
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
    if (!*name)
        return 0;
    for (const char *p = name; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_' && *p != '.' && *p != '+')
            return 0;
    return 1;
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
            } else {
                char *eq = strchr(s, '=');
                if (!eq)
                    die("%s:%d: expected 'key = value'", path, lineno);
                *eq = '\0';
                char *key = strip(s);
                char *value = strip(eq + 1);
                if (section == OPTIONS) {
                    if (!strcmp(key, "Architecture"))
                        snprintf(arch, sizeof(arch), "%s", value);
                    else if (!strcmp(key, "SigLevel"))
                        siglevel = parse_siglevel(value);
                    else if (!strcmp(key, "API"))
                        snprintf(api_base, sizeof(api_base), "%s", value);
                    else if (!strcmp(key, "Prerelease"))
                        allow_prerelease = !strcasecmp(value, "yes") || !strcmp(value, "1");
                } else if (section == TRUSTED) {
                    if (!valid_name(key))
                        die("%s:%d: bad package name '%s'", path, lineno, key);
                    if (sources_total == MAX_SOURCES)
                        die("%s: too many trusted repositories", path);
                    struct source *src = &sources[sources_total];
                    memset(src, 0, sizeof(*src));
                    snprintf(src->name, sizeof(src->name), "%s", key);
                    snprintf(src->url, sizeof(src->url), "%s", value);
                    if (parse_github(value, src->owner_repo, sizeof(src->owner_repo)) < 0)
                        die("%s:%d: '%s' is not a GitHub repository URL", path, lineno, value);
                    int dup = 0;
                    for (int i = 0; i < sources_total; i++)
                        if (!strcmp(sources[i].name, key))
                            dup = 1;
                    if (dup)
                        warn("%s:%d: '%s' is listed twice, the first entry is used", path, lineno, key);
                    else
                        sources_total++;
                } else {
                    die("%s:%d: setting outside of a section", path, lineno);
                }
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
    return 0;
}

void config_load(void)
{
    sources_total = 0;
    config_used[0] = '\0';
    if (opt.config) {
        load_file(opt.config, 1);
        return;
    }
    if (load_file(rp("etc/pkg/pkg.conf"), 0) == 0)
        return;
    load_file(rp("etc/pkg.conf"), 0);
}

static int source_index(const char *name)
{
    for (int i = 0; i < sources_total; i++)
        if (!strcmp(sources[i].name, name))
            return i;
    return -1;
}

const char *source_by_url(const char *url)
{
    char want[96];
    if (parse_github(url, want, sizeof(want)) < 0)
        return NULL;
    for (int i = 0; i < sources_total; i++)
        if (!strcasecmp(sources[i].owner_repo, want))
            return sources[i].name;
    return NULL;
}

static int curl_get(const char *url, const char *dest, int api, int quiet)
{
    if (!strncmp(url, "file://", 7) || url[0] == '/')
        return copy_file(url[0] == '/' ? url : url + 7, dest);
    char tmp[PKG_PATH_MAX + 16];
    snprintf(tmp, sizeof(tmp), "%s.part", dest);
    unlink(tmp);
    char *argv[16];
    int n = 0;
    argv[n++] = "curl";
    argv[n++] = "-fL";
    argv[n++] = "--retry";
    argv[n++] = "2";
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

static int asset_version(const char *asset, const char *name, char *version, size_t cap)
{
    size_t nl = strlen(name);
    if (strncmp(asset, name, nl) != 0 || asset[nl] != '-' || !isdigit((unsigned char)asset[nl + 1]))
        return -1;
    const char *rest = asset + nl + 1;
    char suffix[40];
    const char *arches[2] = {arch, "any"};
    for (int i = 0; i < 2; i++) {
        snprintf(suffix, sizeof(suffix), "-%s.pkg.tar.gz", arches[i]);
        if (ends_with(rest, suffix)) {
            size_t vl = strlen(rest) - strlen(suffix);
            if (!vl || vl + 1 > cap)
                return -1;
            memcpy(version, rest, vl);
            version[vl] = '\0';
            return 0;
        }
    }
    return -1;
}

struct found {
    char version[PKG_VERSION_MAX];
    char tag[64];
    char asset[PKG_FILENAME_MAX];
    char url[PKG_PATH_MAX];
    char sig[PKG_PATH_MAX];
    char desc[PKG_DESC_MAX];
    uint64_t size;
};

static int scan_release(const char *release, const struct source *src, struct found *best)
{
    const char *v;
    if ((v = json_get(release, "draft")) && !strncmp(v, "true", 4))
        return 0;
    if (!allow_prerelease && (v = json_get(release, "prerelease")) && !strncmp(v, "true", 4))
        return 0;
    const char *assets = json_get(release, "assets");
    if (!assets || *assets != '[')
        return 0;
    struct found cand;
    memset(&cand, 0, sizeof(cand));
    int have = 0;
    const char *cursor = assets, *asset;
    while (json_array_next(&cursor, &asset) == 1) {
        char aname[PKG_FILENAME_MAX], version[PKG_VERSION_MAX];
        const char *nv = json_get(asset, "name");
        if (!nv || json_string(nv, aname, sizeof(aname)) < 0)
            continue;
        if (asset_version(aname, src->name, version, sizeof(version)) < 0)
            continue;
        if (have && pkg_vercmp(version, cand.version) <= 0)
            continue;
        const char *uv = json_get(asset, "browser_download_url");
        if (!uv || json_string(uv, cand.url, sizeof(cand.url)) < 0)
            continue;
        snprintf(cand.asset, sizeof(cand.asset), "%s", aname);
        snprintf(cand.version, sizeof(cand.version), "%s", version);
        const char *sv = json_get(asset, "size");
        cand.size = sv ? (uint64_t)json_number(sv) : 0;
        have = 1;
    }
    if (!have)
        return 0;
    char signame[PKG_FILENAME_MAX + 8];
    snprintf(signame, sizeof(signame), "%s.sig", cand.asset);
    cursor = assets;
    cand.sig[0] = '\0';
    while (json_array_next(&cursor, &asset) == 1) {
        char aname[PKG_FILENAME_MAX + 8];
        const char *nv = json_get(asset, "name");
        if (!nv || json_string(nv, aname, sizeof(aname)) < 0 || strcmp(aname, signame) != 0)
            continue;
        const char *uv = json_get(asset, "browser_download_url");
        if (uv)
            json_string(uv, cand.sig, sizeof(cand.sig));
    }
    if ((v = json_get(release, "tag_name")))
        json_string(v, cand.tag, sizeof(cand.tag));
    if ((v = json_get(release, "name")))
        json_string(v, cand.desc, sizeof(cand.desc));
    if (best->version[0] && pkg_vercmp(cand.version, best->version) <= 0)
        return 0;
    *best = cand;
    return 1;
}

static int query_source(const struct source *src, struct found *out, char *why, size_t why_cap)
{
    memset(out, 0, sizeof(*out));
    char url[PKG_PATH_MAX + 128], dest[PKG_PATH_MAX];
    snprintf(dest, sizeof(dest), "%s/%s.json", rp("var/lib/pkg/sync"), src->name);
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
            scan_release(release, src, out);
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
    if (!out->version[0]) {
        if (!releases)
            snprintf(why, why_cap, "no releases in %s", src->url);
        else
            snprintf(why, why_cap, "no %s-<version>-%s.pkg.tar.gz in the releases of %s", src->name, arch, src->url);
        return -1;
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
    src_path(src->name, path, sizeof(path));
    int n = snprintf(body, sizeof(body),
                     "name=%s\nversion=%s\nrepo=%s\ntag=%s\nasset=%s\nurl=%s\nsig=%s\nsize=%llu\ndesc=%s\n",
                     src->name, f->version, src->owner_repo, f->tag, f->asset, f->url, f->sig,
                     (unsigned long long)f->size, f->desc);
    if (n > 0 && (size_t)n < sizeof(body))
        write_text_file(path, body, (size_t)n);
}

int sync_refresh(void)
{
    if (!sources_total)
        die("no trusted repositories in %s", config_used[0] ? config_used : rp("etc/pkg/pkg.conf"));
    msg("Checking the trusted repositories...");
    mkdirs(rp("var/lib/pkg/sync"), 0755);
    int failed = 0;
    for (int i = 0; i < sources_total; i++) {
        const struct source *src = &sources[i];
        struct found f;
        char why[300];
        printf(" %-20s ", src->name);
        fflush(stdout);
        if (query_source(src, &f, why, sizeof(why)) < 0) {
            printf("failed\n");
            warn("%s: %s", src->name, why);
            failed++;
            continue;
        }
        write_src(src, &f);
        printf("%s (%s)\n", f.version, f.tag[0] ? f.tag : "?");
    }
    DIR *d = opendir(rp("var/lib/pkg/sync"));
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            size_t n = strlen(e->d_name);
            char path[PKG_PATH_MAX + 300];
            snprintf(path, sizeof(path), "%s/%s", rp("var/lib/pkg/sync"), e->d_name);
            if (n > 4 && !strcmp(e->d_name + n - 4, ".src")) {
                char name[PKG_NAME_MAX];
                snprintf(name, sizeof(name), "%.*s", (int)(n - 4), e->d_name);
                if (source_index(name) < 0)
                    unlink(path);
            } else if ((n > 3 && !strcmp(e->d_name + n - 3, ".db")) || (n > 7 && !strcmp(e->d_name + n - 7, ".db.sig"))) {
                unlink(path);
            }
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

void syncdb_load(void)
{
    free(sdb.pkgs);
    free(sdb.meta);
    memset(&sdb, 0, sizeof(sdb));
    for (int i = 0; i < sources_total; i++) {
        char path[PKG_PATH_MAX + 80];
        src_path(sources[i].name, path, sizeof(path));
        char *text = read_text_file(path, NULL);
        if (!text)
            continue;
        struct pkg_info info;
        struct sync_meta meta;
        memset(&info, 0, sizeof(info));
        memset(&meta, 0, sizeof(meta));
        char *line = text;
        while (*line) {
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
            if (!eol)
                break;
            line = eol + 1;
        }
        free(text);
        if (strcmp(info.name, sources[i].name) != 0 || !info.version[0] || !meta.url[0] || !info.filename[0])
            continue;
        snprintf(info.arch, sizeof(info.arch), "%s", arch);
        sync_add(&info, &meta);
    }
}

int syncdb_find(const char *name)
{
    for (int i = 0; i < sdb.count; i++)
        if (!strcmp(sdb.pkgs[i].name, name))
            return i;
    return -1;
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
    printf(" %s (%s)\n", p->filename, m->repo);
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
    for (int i = 0; i < sources_total; i++) {
        int s = syncdb_find(sources[i].name);
        printf("%-20s %-40s %s\n", sources[i].name, sources[i].url, s >= 0 ? sdb.pkgs[s].version : "-");
    }
}
