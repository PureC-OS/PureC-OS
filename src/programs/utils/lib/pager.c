#include "common.h"
#include "pager.h"
#include <ctype.h>
#include <termios.h>

void pager_add(struct pager_text *t, const char *line)
{
    if (t->count >= t->cap) {
        t->cap = t->cap ? t->cap * 2 : 256;
        t->lines = realloc(t->lines, sizeof(char *) * (size_t)t->cap);
    }
    t->lines[t->count++] = strdup(line);
}

void pager_add_file(struct pager_text *t, FILE *in)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    while ((len = util_getline(&line, &cap, in)) >= 0) {
        if (len && line[len - 1] == '\n') line[--len] = 0;
        if (len && line[len - 1] == '\r') line[--len] = 0;
        char *out = malloc((size_t)len * 8 + 1);
        size_t o = 0;
        int col = 0;
        for (ssize_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)line[i];
            if (c == '\t') {
                do { out[o++] = ' '; col++; } while (col % 8);
            } else if (c == 0x1b) {
                out[o++] = (char)c;
            } else if (c < 32 || c == 127) {
                out[o++] = '^';
                out[o++] = (char)(c == 127 ? '?' : c + 64);
                col += 2;
            } else {
                out[o++] = (char)c;
                if ((c & 0xC0) != 0x80) col++;
            }
        }
        out[o] = 0;
        pager_add(t, out);
        free(out);
    }
    free(line);
}

void pager_free(struct pager_text *t)
{
    for (int i = 0; i < t->count; i++) free(t->lines[i]);
    free(t->lines);
    memset(t, 0, sizeof(*t));
}

struct vline {
    int src;
    int start;
    int end;
    char sgr[48];
};

static struct vline *vlines;
static int nvlines, vcap;

static void push_vline(int src, int start, int end, const char *sgr)
{
    if (nvlines >= vcap) {
        vcap = vcap ? vcap * 2 : 512;
        vlines = realloc(vlines, sizeof(struct vline) * (size_t)vcap);
    }
    struct vline *v = &vlines[nvlines++];
    v->src = src;
    v->start = start;
    v->end = end;
    snprintf(v->sgr, sizeof(v->sgr), "%s", sgr);
}

static void wrap(struct pager_text *t, int width)
{
    nvlines = 0;
    for (int i = 0; i < t->count; i++) {
        const char *s = t->lines[i];
        int start = 0, col = 0, p = 0;
        char sgr[48] = "", start_sgr[48] = "";
        while (s[p]) {
            if (s[p] == 0x1b && s[p + 1] == '[') {
                int q = p + 2;
                while (s[q] && !isalpha((unsigned char)s[q])) q++;
                if (s[q] == 'm') {
                    int len = q + 1 - p;
                    if (len == 4 && s[p + 2] == '0') sgr[0] = 0;
                    else if ((int)strlen(sgr) + len < (int)sizeof(sgr)) strncat(sgr, s + p, (size_t)len);
                }
                p = s[q] ? q + 1 : q;
                continue;
            }
            if (((unsigned char)s[p] & 0xC0) != 0x80) {
                if (col >= width) {
                    push_vline(i, start, p, start_sgr);
                    start = p;
                    col = 0;
                    snprintf(start_sgr, sizeof(start_sgr), "%s", sgr);
                }
                col++;
            }
            p++;
        }
        push_vline(i, start, p, start_sgr);
    }
}

static struct termios saved_mode;
static int key_fd = -1;

static int read_byte(int timeout_tenths)
{
    struct termios mode;
    tcgetattr(key_fd, &mode);
    mode.c_cc[VMIN] = timeout_tenths ? 0 : 1;
    mode.c_cc[VTIME] = (cc_t)timeout_tenths;
    tcsetattr(key_fd, TCSANOW, &mode);
    unsigned char c;
    ssize_t n = read(key_fd, &c, 1);
    return n == 1 ? c : -1;
}

enum { K_UP = 1000, K_DOWN, K_PGUP, K_PGDN, K_HOME, K_END, K_ESC };

static int read_key(void)
{
    int c = read_byte(0);
    if (c != 0x1b) return c;
    int c2 = read_byte(1);
    if (c2 < 0) return K_ESC;
    if (c2 != '[' && c2 != 'O') return K_ESC;
    int c3 = read_byte(1);
    if (c3 == 'A') return K_UP;
    if (c3 == 'B') return K_DOWN;
    if (c3 == 'H') return K_HOME;
    if (c3 == 'F') return K_END;
    if (c3 >= '0' && c3 <= '9') {
        int c4 = read_byte(1);
        while (c4 >= 0 && c4 != '~') c4 = read_byte(1);
        if (c3 == '5') return K_PGUP;
        if (c3 == '6') return K_PGDN;
        if (c3 == '1' || c3 == '7') return K_HOME;
        if (c3 == '4' || c3 == '8') return K_END;
    }
    return -1;
}

static bool line_has(const char *line, const char *needle)
{
    char plain[4096];
    int o = 0;
    for (int p = 0; line[p] && o < (int)sizeof(plain) - 1; p++) {
        if (line[p] == 0x1b && line[p + 1] == '[') {
            while (line[p] && !isalpha((unsigned char)line[p])) p++;
            if (!line[p]) break;
            continue;
        }
        plain[o++] = line[p];
    }
    plain[o] = 0;
    size_t nl = strlen(needle);
    if (!nl) return false;
    for (int i = 0; plain[i]; i++)
        if (!strncasecmp(plain + i, needle, nl)) return true;
    return false;
}

static void draw(struct pager_text *t, int top, int rows, int width, const char *title, const char *msg, const char *search)
{
    printf("\x1b[H");
    for (int r = 0; r < rows - 1; r++) {
        int idx = top + r;
        printf("\x1b[2K");
        if (idx < nvlines) {
            struct vline *v = &vlines[idx];
            const char *s = t->lines[v->src];
            fputs(v->sgr, stdout);
            if (search && *search && line_has(s, search)) fputs("\x1b[7m", stdout);
            fwrite(s + v->start, 1, (size_t)(v->end - v->start), stdout);
            fputs(C_RESET, stdout);
        } else {
            fputs(C_GRAY "~" C_RESET, stdout);
        }
        fputs("\r\n", stdout);
    }
    int last = top + rows - 1;
    if (last > nvlines) last = nvlines;
    char status[512];
    if (msg) snprintf(status, sizeof(status), " %s", msg);
    else snprintf(status, sizeof(status), " %s  lines %d-%d/%d %s  q quit, / search, n next, g/G top/end",
                  title, nvlines ? top + 1 : 0, last, nvlines, last >= nvlines ? "(END)" : "");
    int len = (int)strlen(status);
    if (len > width) status[width] = 0;
    printf("\x1b[2K\x1b[7m%s", status);
    for (int i = len; i < width; i++) putchar(' ');
    printf("\x1b[0m");
    fflush(stdout);
}

static bool prompt_search(char *out, size_t cap, int rows, int width)
{
    size_t len = 0;
    out[0] = 0;
    for (;;) {
        printf("\x1b[%d;1H\x1b[2K/%s", rows, out);
        fflush(stdout);
        (void)width;
        int c = read_key();
        if (c == '\r' || c == '\n') return true;
        if (c == K_ESC || c == 3 || c < 0) return false;
        if (c == 127 || c == 8) {
            if (len) out[--len] = 0;
            else return false;
            continue;
        }
        if (c >= 32 && c < 256 && len + 1 < cap) {
            out[len++] = (char)c;
            out[len] = 0;
        }
    }
}

static int find_from(struct pager_text *t, int from, int dir, const char *needle)
{
    for (int i = from; i >= 0 && i < nvlines; i += dir) {
        if (i > 0 && vlines[i].src == vlines[i - 1].src && dir > 0 && i != from) continue;
        if (line_has(t->lines[vlines[i].src], needle)) return i;
    }
    return -1;
}

int pager_show(struct pager_text *t, const char *title, bool always)
{
    if (!isatty(1)) {
        for (int i = 0; i < t->count; i++) puts(t->lines[i]);
        return 0;
    }
    key_fd = isatty(0) ? 0 : isatty(2) ? 2 : -1;
    int width = util_term_width();
    int rows = util_term_height();
    if (key_fd < 0 || (!always && t->count < rows)) {
        for (int i = 0; i < t->count; i++) {
            fputs(t->lines[i], stdout);
            fputs(C_RESET "\n", stdout);
        }
        return 0;
    }
    if (tcgetattr(key_fd, &saved_mode) != 0) {
        for (int i = 0; i < t->count; i++) puts(t->lines[i]);
        return 0;
    }
    struct termios raw = saved_mode;
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= ~(tcflag_t)(ICRNL | IXON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(key_fd, TCSANOW, &raw);
    printf("\x1b[?1049h\x1b[?25l\x1b[H\x1b[2J");
    wrap(t, width);
    int top = 0;
    char search[128] = "";
    const char *msg = NULL;
    for (;;) {
        int nw = util_term_width(), nr = util_term_height();
        if (nw != width || nr != rows) {
            int src = top < nvlines ? vlines[top].src : 0;
            width = nw;
            rows = nr;
            wrap(t, width);
            top = 0;
            while (top + 1 < nvlines && vlines[top].src < src) top++;
            printf("\x1b[2J");
        }
        int page = rows - 1;
        int max_top = nvlines - page;
        if (max_top < 0) max_top = 0;
        if (top > max_top) top = max_top;
        if (top < 0) top = 0;
        draw(t, top, rows, width, title, msg, search);
        msg = NULL;
        int c = read_key();
        if (c < 0) continue;
        if (c == 'q' || c == 'Q' || c == K_ESC || c == 3) break;
        else if (c == 'j' || c == K_DOWN || c == '\r' || c == '\n' || c == 14 || c == 5 || c == 'e') top++;
        else if (c == 'k' || c == K_UP || c == 'y' || c == 16 || c == 25) top--;
        else if (c == ' ' || c == 'f' || c == K_PGDN || c == 6 || c == 22) top += page;
        else if (c == 'b' || c == K_PGUP || c == 2) top -= page;
        else if (c == 'd' || c == 4) top += page / 2;
        else if (c == 'u' || c == 21) top -= page / 2;
        else if (c == 'g' || c == '<' || c == K_HOME) top = 0;
        else if (c == 'G' || c == '>' || c == K_END) top = max_top;
        else if (c == '/' || c == '?') {
            char query[128];
            if (prompt_search(query, sizeof(query), rows, width) && query[0]) {
                snprintf(search, sizeof(search), "%s", query);
                int hit = find_from(t, c == '/' ? top + 1 : top - 1, c == '/' ? 1 : -1, search);
                if (hit >= 0) top = hit;
                else msg = "Pattern not found";
            }
        } else if ((c == 'n' || c == 'N') && search[0]) {
            int hit = find_from(t, c == 'n' ? top + 1 : top - 1, c == 'n' ? 1 : -1, search);
            if (hit >= 0) top = hit;
            else msg = "Pattern not found";
        } else if (c == 'h' || c == 'H') {
            msg = "j/k line  space/b page  d/u half  g/G top/end  /text search  n/N next/prev  q quit";
        }
    }
    printf("\x1b[0m\x1b[?25h\x1b[?1049l");
    fflush(stdout);
    tcsetattr(key_fd, TCSANOW, &saved_mode);
    free(vlines);
    vlines = NULL;
    nvlines = vcap = 0;
    return 0;
}
