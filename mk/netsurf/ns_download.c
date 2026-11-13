#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils/errors.h"
#include "utils/nsurl.h"
#include "desktop/download.h"
#include "desktop/gui_table.h"
#include "netsurf/download.h"
#include "netsurf/window.h"
#include "netsurf/misc.h"

#define PROGRESS_STEP (256u * 1024u)

struct gui_download_window {
    struct download_context *ctx;
    FILE *file;
    struct gui_window *parent;
    char path[512];
    char part[520];
    char name[256];
    unsigned long long total;
    unsigned long long received;
    unsigned long long reported;
    bool failed;
};

static struct gui_window_table *window_table;
static struct gui_misc_table *misc_table;

static void release(void *p)
{
    struct gui_download_window *dw = p;
    download_context_destroy(dw->ctx);
    free(dw);
}

static void finish(struct gui_download_window *dw)
{
    if (dw->file)
        fclose(dw->file);
    dw->file = NULL;
    if (misc_table && misc_table->schedule &&
        misc_table->schedule(0, release, dw) == NSERROR_OK)
        return;
    release(dw);
}

static void status(struct gui_download_window *dw, const char *text)
{
    if (window_table && window_table->set_status && dw->parent)
        window_table->set_status(dw->parent, text);
}

static void human(char *out, size_t cap, unsigned long long bytes)
{
    if (bytes >= 1024ULL * 1024ULL)
        snprintf(out, cap, "%llu.%llu MB", bytes / (1024ULL * 1024ULL),
                 bytes % (1024ULL * 1024ULL) * 10 / (1024ULL * 1024ULL));
    else if (bytes >= 1024)
        snprintf(out, cap, "%llu KB", bytes / 1024);
    else
        snprintf(out, cap, "%llu B", bytes);
}

static void progress(struct gui_download_window *dw)
{
    char got[32], all[32], text[400];
    human(got, sizeof(got), dw->received);
    if (dw->total) {
        human(all, sizeof(all), dw->total);
        snprintf(text, sizeof(text), "Downloading %s: %s / %s (%llu%%)", dw->name, got, all,
                 dw->received * 100 / dw->total);
    } else {
        snprintf(text, sizeof(text), "Downloading %s: %s", dw->name, got);
    }
    status(dw, text);
}

static void clean_name(const char *suggested, char *out, size_t cap)
{
    size_t n = 0;
    for (const char *p = suggested ? suggested : ""; *p && n + 1 < cap; p++) {
        char c = *p;
        if (c == '/' || c == '\\' || c < 32)
            c = '_';
        out[n++] = c;
    }
    out[n] = 0;
    while (n && (out[n - 1] == ' ' || out[n - 1] == '.'))
        out[--n] = 0;
    if (!n || strcmp(out, ".") == 0 || strcmp(out, "..") == 0)
        snprintf(out, cap, "download");
}

static void downloads_dir(char *out, size_t cap)
{
    const char *home = getenv("HOME");
    if (!home || !*home)
        home = getuid() == 0 ? "/root" : "/";
    snprintf(out, cap, "%s/Downloads", strcmp(home, "/") == 0 ? "" : home);
    mkdir(out, 0755);
    struct stat st;
    if (stat(out, &st) != 0 || !S_ISDIR(st.st_mode))
        snprintf(out, cap, "%s", home);
}

static void unique_path(struct gui_download_window *dw, const char *dir)
{
    char base[256], ext[64] = "";
    snprintf(base, sizeof(base), "%s", dw->name);
    char *dot = strrchr(base, '.');
    if (dot && dot != base && strlen(dot) < sizeof(ext)) {
        snprintf(ext, sizeof(ext), "%s", dot);
        *dot = 0;
    }
    struct stat st;
    snprintf(dw->path, sizeof(dw->path), "%s/%s%s", dir, base, ext);
    for (int i = 1; i < 1000 && stat(dw->path, &st) == 0; i++)
        snprintf(dw->path, sizeof(dw->path), "%s/%s (%d)%s", dir, base, i, ext);
    const char *slash = strrchr(dw->path, '/');
    snprintf(dw->name, sizeof(dw->name), "%s", slash ? slash + 1 : dw->path);
    snprintf(dw->part, sizeof(dw->part), "%s.part", dw->path);
}

static struct gui_download_window *download_create(struct download_context *ctx,
                                                   struct gui_window *parent)
{
    struct gui_download_window *dw = calloc(1, sizeof(*dw));
    if (!dw)
        return NULL;
    dw->parent = parent;
    dw->ctx = ctx;
    dw->total = download_context_get_total_length(ctx);
    clean_name(download_context_get_filename(ctx), dw->name, sizeof(dw->name));
    char dir[400];
    downloads_dir(dir, sizeof(dir));
    unique_path(dw, dir);
    dw->file = fopen(dw->part, "wb");
    if (!dw->file) {
        char text[600];
        snprintf(text, sizeof(text), "Cannot save %s: %s", dw->path, strerror(errno));
        status(dw, text);
        fprintf(stderr, "netsurf: %s\n", text);
        free(dw);
        return NULL;
    }
    progress(dw);
    return dw;
}

static nserror download_data(struct gui_download_window *dw, const char *data, unsigned int size)
{
    if (!dw->file || dw->failed)
        return NSERROR_SAVE_FAILED;
    if (fwrite(data, 1, size, dw->file) != size) {
        dw->failed = true;
        status(dw, "Download failed: disk write error");
        return NSERROR_SAVE_FAILED;
    }
    dw->received += size;
    if (dw->received - dw->reported >= PROGRESS_STEP) {
        dw->reported = dw->received;
        progress(dw);
    }
    return NSERROR_OK;
}

static void download_error(struct gui_download_window *dw, const char *error_msg)
{
    char text[400];
    snprintf(text, sizeof(text), "Download of %s failed: %s", dw->name,
             error_msg ? error_msg : "error");
    status(dw, text);
    fprintf(stderr, "netsurf: %s\n", text);
    dw->failed = true;
    if (dw->file)
        fclose(dw->file);
    dw->file = NULL;
    unlink(dw->part);
    finish(dw);
}

static void download_done(struct gui_download_window *dw)
{
    if (dw->file)
        fclose(dw->file);
    dw->file = NULL;
    char text[700];
    if (dw->failed) {
        unlink(dw->part);
    } else if (rename(dw->part, dw->path) == 0) {
        char size[32];
        human(size, sizeof(size), dw->received);
        snprintf(text, sizeof(text), "Saved %s (%s)", dw->path, size);
        status(dw, text);
        fprintf(stderr, "netsurf: %s\n", text);
    } else {
        snprintf(text, sizeof(text), "Cannot rename %s: %s", dw->part, strerror(errno));
        status(dw, text);
    }
    finish(dw);
}

static struct gui_download_table purec_download_table = {
    .create = download_create,
    .data = download_data,
    .error = download_error,
    .done = download_done,
};

nserror __real_netsurf_register(struct netsurf_table *table);

nserror __wrap_netsurf_register(struct netsurf_table *table)
{
    if (table) {
        window_table = table->window;
        misc_table = table->misc;
        if (!table->download)
            table->download = &purec_download_table;
    }
    return __real_netsurf_register(table);
}
