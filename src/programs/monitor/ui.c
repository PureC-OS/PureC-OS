#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C_BASE 0x1E1E2E
#define C_MANTLE 0x181825
#define C_CRUST 0x11111B
#define C_SURF0 0x313244
#define C_SURF1 0x45475A
#define C_SURF2 0x585B70
#define C_OVER0 0x6C7086
#define C_SUB0 0xA6ADC8
#define C_TEXT 0xCDD6F4
#define C_BLUE 0x89B4FA
#define C_LAV 0xB4BEFE
#define C_GREEN 0xA6E3A1
#define C_YELLOW 0xF9E2AF
#define C_PEACH 0xFAB387
#define C_RED 0xF38BA8
#define C_MAUVE 0xCBA6F7
#define C_TEAL 0x94E2D5

#define TOP 34
#define FOOT 26
#define PAD 14
#define LH 16
#define DETAIL_H 120
#define LIST_W 236

static const char *const tab_names[TAB_COUNT] = {"1 Processes", "2 System", "3 /proc"};
static struct ui_state *sort_context;

static uint32_t pct_color(int pct) {
  if (pct < 60)
    return cv_mix(C_GREEN, C_YELLOW, pct, 60);
  return cv_mix(C_YELLOW, C_RED, pct - 60, 40);
}

static void fmt_kb(char *out, size_t cap, uint64_t kb) {
  if (kb >= 1024ull * 1024ull * 10ull)
    snprintf(out, cap, "%lluG", (unsigned long long)(kb / (1024ull * 1024ull)));
  else if (kb >= 1024ull * 1024ull)
    snprintf(out, cap, "%llu.%lluG", (unsigned long long)(kb / (1024ull * 1024ull)),
             (unsigned long long)((kb % (1024ull * 1024ull)) * 10 / (1024ull * 1024ull)));
  else if (kb >= 1024ull * 10ull)
    snprintf(out, cap, "%lluM", (unsigned long long)(kb / 1024ull));
  else if (kb >= 1024ull)
    snprintf(out, cap, "%llu.%lluM", (unsigned long long)(kb / 1024ull),
             (unsigned long long)((kb % 1024ull) * 10 / 1024ull));
  else
    snprintf(out, cap, "%lluK", (unsigned long long)kb);
}

static void fmt_time(char *out, size_t cap, uint64_t ms) {
  uint64_t total_s = ms / 1000;
  if (total_s >= 3600)
    snprintf(out, cap, "%lluh%02llum", (unsigned long long)(total_s / 3600),
             (unsigned long long)((total_s / 60) % 60));
  else
    snprintf(out, cap, "%llu:%02llu.%02llu", (unsigned long long)(total_s / 60),
             (unsigned long long)(total_s % 60), (unsigned long long)((ms % 1000) / 10));
}

static void fmt_uptime(char *out, size_t cap, uint64_t cs) {
  uint64_t s = cs / 100;
  uint64_t d = s / 86400;
  s %= 86400;
  if (d)
    snprintf(out, cap, "%llud %02llu:%02llu:%02llu", (unsigned long long)d,
             (unsigned long long)(s / 3600), (unsigned long long)((s / 60) % 60),
             (unsigned long long)(s % 60));
  else
    snprintf(out, cap, "%02llu:%02llu:%02llu", (unsigned long long)(s / 3600),
             (unsigned long long)((s / 60) % 60), (unsigned long long)(s % 60));
}

static void fmt_size(char *out, size_t cap, uint64_t sectors, uint32_t sector_size) {
  uint64_t bytes = sectors * (sector_size ? sector_size : 512);
  uint64_t kb = bytes / 1024;
  fmt_kb(out, cap, kb);
}

static bool contains_nocase(const char *haystack, const char *needle) {
  if (!needle[0])
    return true;
  for (const char *h = haystack; *h; h++) {
    const char *a = h;
    const char *b = needle;
    while (*a && *b) {
      char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
      char y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
      if (x != y)
        break;
      a++;
      b++;
    }
    if (!*b)
      return true;
  }
  return false;
}

static int compare_procs(const void *pa, const void *pb) {
  const struct ui_state *u = sort_context;
  const struct mon_proc *a = &u->model.procs[*(const int *)pa];
  const struct mon_proc *b = &u->model.procs[*(const int *)pb];
  int64_t diff = 0;
  switch (u->sort_key) {
  case SORT_CPU: diff = (int64_t)a->cpu - (int64_t)b->cpu; break;
  case SORT_MEM: diff = (int64_t)a->rss_kb - (int64_t)b->rss_kb; break;
  case SORT_TIME: diff = (int64_t)a->time_ms - (int64_t)b->time_ms; break;
  case SORT_NAME: diff = strcmp(a->name, b->name); break;
  default: diff = (int64_t)a->pid - (int64_t)b->pid; break;
  }
  if (u->sort_key == SORT_NAME || u->sort_key == SORT_PID) {
    if (u->sort_desc)
      diff = -diff;
  } else if (u->sort_desc) {
    diff = -diff;
  }
  if (diff)
    return diff < 0 ? -1 : 1;
  return a->pid < b->pid ? -1 : (a->pid > b->pid ? 1 : 0);
}

static void build_tree(struct ui_state *u, const int *list, int count) {
  int out = 0;
  bool placed[MON_MAX_PROCS];
  memset(placed, 0, sizeof(placed));
  int stack[48], depth_of[48];
  for (int pass = 0; pass < 2 && out < count; pass++) {
    for (int ri = 0; ri < count && out < count; ri++) {
      int root = list[ri];
      if (placed[root])
        continue;
      struct mon_proc *rp = &u->model.procs[root];
      bool has_parent = false;
      for (int k = 0; k < count; k++)
        if (list[k] != root && u->model.procs[list[k]].pid == rp->ppid && rp->ppid != rp->pid)
          has_parent = true;
      if (pass == 0 && has_parent)
        continue;
      int sp = 0;
      stack[sp] = root;
      depth_of[sp++] = 0;
      while (sp > 0 && out < count) {
        sp--;
        int idx = stack[sp];
        int depth = depth_of[sp];
        if (placed[idx])
          continue;
        placed[idx] = true;
        u->model.procs[idx].depth = depth;
        u->order[out++] = idx;
        for (int k = count - 1; k >= 0; k--) {
          int child = list[k];
          if (!placed[child] && u->model.procs[child].ppid == u->model.procs[idx].pid &&
              child != idx && sp < 47) {
            stack[sp] = child;
            depth_of[sp++] = depth < 15 ? depth + 1 : depth;
          }
        }
      }
    }
  }
}

static void rebuild_order(struct ui_state *u) {
  int list[MON_MAX_PROCS];
  int count = 0;
  for (int i = 0; i < u->model.proc_count; i++) {
    struct mon_proc *p = &u->model.procs[i];
    p->depth = 0;
    if (u->filter[0] && !contains_nocase(p->name, u->filter) && !contains_nocase(p->cmd, u->filter))
      continue;
    list[count++] = i;
  }
  sort_context = u;
  if (u->tree) {
    int saved_key = u->sort_key;
    bool saved_desc = u->sort_desc;
    u->sort_key = SORT_PID;
    u->sort_desc = false;
    qsort(list, (size_t)count, sizeof(int), compare_procs);
    u->sort_key = saved_key;
    u->sort_desc = saved_desc;
    build_tree(u, list, count);
  } else {
    qsort(list, (size_t)count, sizeof(int), compare_procs);
    memcpy(u->order, list, sizeof(int) * (size_t)count);
  }
  u->visible = count;
  int found = -1;
  for (int i = 0; i < count; i++)
    if (u->model.procs[u->order[i]].pid == u->sel_pid) {
      found = i;
      break;
    }
  if (found >= 0)
    u->sel = found;
  else if (count) {
    if (u->sel >= count)
      u->sel = count - 1;
    if (u->sel < 0)
      u->sel = 0;
    u->sel_pid = u->model.procs[u->order[u->sel]].pid;
  } else {
    u->sel = 0;
  }
}

static void load_detail(struct ui_state *u) {
  u->detail[0] = 0;
  u->detail_pid = 0;
  if (!u->details || u->sel >= u->visible)
    return;
  uint32_t pid = u->model.procs[u->order[u->sel]].pid;
  char path[48], part[640];
  size_t used = 0;
  static const char *const files[] = {"status", "cmdline", "cwd"};
  for (int i = 0; i < 3; i++) {
    snprintf(path, sizeof(path), "%u/%s", pid, files[i]);
    int n = mon_read_proc(path, part, sizeof(part));
    if (n <= 0)
      continue;
    if (i > 0 && used + 24 < sizeof(u->detail))
      used += (size_t)snprintf(u->detail + used, sizeof(u->detail) - used, "%s:\t", files[i]);
    for (int k = 0; k < n && used + 1 < sizeof(u->detail); k++)
      u->detail[used++] = part[k];
    u->detail[used] = 0;
  }
  u->detail_pid = pid;
}

static int browse_rank(const struct mon_entry *e) {
  return e->is_dir ? 1 : 0;
}

static int browse_compare(const void *pa, const void *pb) {
  const struct mon_entry *a = pa;
  const struct mon_entry *b = pb;
  int ra = browse_rank(a), rb = browse_rank(b);
  if (ra != rb)
    return ra - rb;
  if (a->is_dir && b->is_dir) {
    long x = strtol(a->name, 0, 10), y = strtol(b->name, 0, 10);
    return x < y ? -1 : (x > y ? 1 : 0);
  }
  return strcmp(a->name, b->name);
}

static void tab_expand(char *dst, int cap, const char *src, int n) {
  int out = 0, col = 0;
  for (int i = 0; i < n && out + 1 < cap; i++) {
    if (src[i] == '\t') {
      int fill = 8 - (col % 8);
      while (fill-- && out + 1 < cap) {
        dst[out++] = ' ';
        col++;
      }
    } else {
      dst[out++] = src[i];
      col = src[i] == '\n' ? 0 : col + 1;
    }
  }
  dst[out] = 0;
}

static void index_lines(struct ui_state *u) {
  u->line_count = 0;
  u->line_start[u->line_count++] = 0;
  for (int i = 0; i < u->text_len && u->line_count < UI_LINES_MAX; i++)
    if (u->text[i] == '\n' && i + 1 < u->text_len)
      u->line_start[u->line_count++] = i + 1;
}

static void load_browse_text(struct ui_state *u) {
  static char raw[8192];
  static struct mon_entry children[48];
  u->text[0] = 0;
  u->text_len = 0;
  if (u->browse_sel < 0 || u->browse_sel >= u->browse_count) {
    index_lines(u);
    return;
  }
  const struct mon_entry *e = &u->browse[u->browse_sel];
  if (!e->is_dir) {
    int n = mon_read_proc(e->name, raw, sizeof(raw));
    if (n < 0)
      snprintf(raw, sizeof(raw), "cannot read /proc/%s\n", e->name);
    tab_expand(u->text, sizeof(u->text), raw, (int)strlen(raw));
  } else {
    int kids = mon_list_proc(e->name, children, 48);
    size_t used = 0;
    for (int i = 0; i < kids && used + 64 < sizeof(u->text); i++) {
      char path[96];
      snprintf(path, sizeof(path), "%s/%s", e->name, children[i].name);
      used += (size_t)snprintf(u->text + used, sizeof(u->text) - used, "==> /proc/%s <==\n", path);
      int n = mon_read_proc(path, raw, sizeof(raw));
      if (n > 0) {
        char expanded[8192 + 512];
        tab_expand(expanded, sizeof(expanded), raw, n);
        size_t len = strlen(expanded);
        if (used + len + 2 < sizeof(u->text)) {
          memcpy(u->text + used, expanded, len);
          used += len;
          if (len && expanded[len - 1] != '\n')
            u->text[used++] = '\n';
        }
      }
      if (used + 2 < sizeof(u->text))
        u->text[used++] = '\n';
      u->text[used] = 0;
    }
  }
  u->text_len = (int)strlen(u->text);
  index_lines(u);
}

static void refresh_browse(struct ui_state *u) {
  char keep[MON_BROWSE_NAME];
  keep[0] = 0;
  if (u->browse_sel >= 0 && u->browse_sel < u->browse_count)
    strncpy(keep, u->browse[u->browse_sel].name, sizeof(keep) - 1);
  int n = mon_list_proc("", u->browse, MON_BROWSE_MAX);
  if (n < 0)
    n = 0;
  u->browse_count = n;
  qsort(u->browse, (size_t)n, sizeof(u->browse[0]), browse_compare);
  int found = -1;
  for (int i = 0; i < n; i++)
    if (keep[0] && !strcmp(u->browse[i].name, keep))
      found = i;
  if (found >= 0)
    u->browse_sel = found;
  else if (u->browse_sel >= n)
    u->browse_sel = n ? n - 1 : 0;
  load_browse_text(u);
}

void ui_init(struct ui_state *u) {
  memset(u, 0, sizeof(*u));
  u->sort_key = SORT_CPU;
  u->sort_desc = true;
  u->browse_sel = 0;
  mon_refresh(&u->model);
  mon_refresh(&u->model);
  ui_refresh(u);
}

void ui_refresh(struct ui_state *u) {
  mon_refresh(&u->model);
  rebuild_order(u);
  if (u->details)
    load_detail(u);
  if (u->tab == TAB_PROC)
    refresh_browse(u);
  if (u->message_age < 1000)
    u->message_age++;
}

static void set_message(struct ui_state *u, const char *text) {
  snprintf(u->message, sizeof(u->message), "%s", text);
  u->message_age = 0;
}

static void draw_bar(struct canvas *c, int x, int y, int w, int h, int pct) {
  if (pct < 0)
    pct = 0;
  if (pct > 100)
    pct = 100;
  cv_rrect(c, x, y, w, h, 3, C_SURF0);
  int fill = (w - 2) * pct / 100;
  for (int i = 0; i < fill; i++)
    cv_fill(c, x + 1 + i, y + 1, 1, h - 2, pct_color(i * 100 / (w > 2 ? w - 2 : 1)));
}

static int right_align_x(const char *text, int right) {
  return right - cv_text_width(text, 8);
}

static void draw_topbar(struct ui_state *u, struct canvas *c) {
  cv_fill(c, 0, 0, c->w, TOP, C_CRUST);
  cv_text(c, PAD, 9, "htop", C_MAUVE, 16);
  cv_text(c, PAD + 74, 14, "PureC OS", C_OVER0, 8);
  int x = 190;
  for (int i = 0; i < TAB_COUNT; i++) {
    int w = cv_text_width(tab_names[i], 8) + 28;
    u->tab_x[i] = x;
    if (i == u->tab) {
      cv_rrect(c, x, 6, w, TOP - 6 + 8, 7, C_BASE);
      cv_fill(c, x + 8, 6, w - 16, 2, C_BLUE);
      cv_text(c, x + 14, 14, tab_names[i], C_TEXT, 8);
    } else {
      cv_text(c, x + 14, 14, tab_names[i], C_OVER0, 8);
    }
    x += w + 4;
  }
  u->tab_x[TAB_COUNT] = x;
  char up[40], info[96];
  fmt_uptime(up, sizeof(up), u->model.uptime_cs);
  snprintf(info, sizeof(info), "up %s   tasks %d, %d running", up, u->model.tasks_total,
           u->model.tasks_running);
  cv_text(c, right_align_x(info, c->w - PAD), 14, info, C_SUB0, 8);
}

static void chip(struct canvas *c, int *x, int y, const char *key, const char *label) {
  int kw = cv_text_width(key, 8) + 8;
  int lw = cv_text_width(label, 8);
  if (*x + kw + lw + 20 > c->w)
    return;
  cv_rrect(c, *x, y + 3, kw, 16, 3, C_BLUE);
  cv_text(c, *x + 4, y + 7, key, C_CRUST, 8);
  cv_text(c, *x + kw + 5, y + 7, label, C_SUB0, 8);
  *x += kw + lw + 16;
}

static void draw_footer(struct ui_state *u, struct canvas *c) {
  int y = c->h - FOOT;
  cv_fill(c, 0, y, c->w, FOOT, C_CRUST);
  int x = PAD;
  if (u->filter_mode) {
    char line[64];
    snprintf(line, sizeof(line), "Filter: %s_", u->filter);
    cv_text(c, x, y + 9, line, C_YELLOW, 8);
    cv_text(c, right_align_x("Enter apply   Esc clear", c->w - PAD), y + 9,
            "Enter apply   Esc clear", C_OVER0, 8);
    return;
  }
  if (u->message[0] && u->message_age < 4) {
    cv_text(c, x, y + 9, u->message, C_YELLOW, 8);
    return;
  }
  if (u->tab == TAB_PROCS) {
    chip(c, &x, y, "1-3", "Tabs");
    chip(c, &x, y, "Up/Dn", "Select");
    chip(c, &x, y, "t", "Tree");
    chip(c, &x, y, "c", "CPU");
    chip(c, &x, y, "m", "Mem");
    chip(c, &x, y, "p", "PID");
    chip(c, &x, y, "n", "Name");
    chip(c, &x, y, "/", "Filter");
    chip(c, &x, y, "k", "Kill");
    chip(c, &x, y, "Enter", "Details");
    chip(c, &x, y, "q", "Quit");
  } else if (u->tab == TAB_SYSTEM) {
    chip(c, &x, y, "1-3", "Tabs");
    chip(c, &x, y, "Up/Dn", "Scroll");
    chip(c, &x, y, "Wheel", "Scroll");
    chip(c, &x, y, "q", "Quit");
  } else {
    chip(c, &x, y, "1-3", "Tabs");
    chip(c, &x, y, "Up/Dn", "File");
    chip(c, &x, y, "PgUp/PgDn", "Content");
    chip(c, &x, y, "Wheel", "Scroll");
    chip(c, &x, y, "q", "Quit");
  }
}

static int draw_cores(struct ui_state *u, struct canvas *c, int x, int y, int w) {
  const struct mon_model *m = &u->model;
  int n = m->cpu_count;
  if (n <= 0) {
    cv_text(c, x, y + 4, "CPU", C_TEAL, 8);
    draw_bar(c, x + 40, y + 3, w - 40 - 48, 10, (int)m->cpu_total_pct);
    char t[8];
    snprintf(t, sizeof(t), "%3u%%", m->cpu_total_pct);
    cv_text(c, x + w - 40, y + 4, t, C_TEXT, 8);
    return LH;
  }
  int cols = n > 16 ? 3 : (n > 8 ? 2 : 1);
  int rows = (n + cols - 1) / cols;
  int gap = 18;
  int colw = (w - (cols - 1) * gap) / cols;
  for (int i = 0; i < n; i++) {
    int col = i / rows, row = i % rows;
    int bx = x + col * (colw + gap);
    int by = y + row * LH;
    char label[16], t[8];
    snprintf(label, sizeof(label), "%2d", i);
    cv_text(c, bx, by + 4, label, C_TEAL, 8);
    int bar_w = colw - 24 - 44;
    if (bar_w < 20)
      bar_w = 20;
    if (!m->cpu[i].online) {
      cv_rrect(c, bx + 24, by + 3, bar_w, 10, 3, C_SURF0);
      cv_text(c, bx + 24 + bar_w + 6, by + 4, "off", C_OVER0, 8);
      continue;
    }
    draw_bar(c, bx + 24, by + 3, bar_w, 10, (int)m->cpu[i].pct);
    snprintf(t, sizeof(t), "%3u%%", m->cpu[i].pct);
    cv_text(c, bx + 24 + bar_w + 6, by + 4, t, pct_color((int)m->cpu[i].pct), 8);
  }
  return rows * LH;
}

static void draw_history(struct ui_state *u, struct canvas *c, int x, int y, int w, int h) {
  const struct mon_model *m = &u->model;
  cv_frame(c, x, y, w, h, 5, C_SURF0, C_MANTLE);
  int inner_w = w - 8, inner_h = h - 8;
  int slots = inner_w / 3;
  if (slots > MON_HIST)
    slots = MON_HIST;
  int start = m->hist_len > slots ? m->hist_len - slots : 0;
  int count = m->hist_len - start;
  for (int i = 0; i < count; i++) {
    int pct = m->hist[start + i];
    int bh = pct * inner_h / 100;
    if (bh < 1 && pct > 0)
      bh = 1;
    int bx = x + 4 + inner_w - (count - i) * 3;
    cv_fill(c, bx, y + 4 + inner_h - bh, 2, bh, pct_color(pct));
  }
  cv_text(c, x + 8, y + 6, "CPU history", C_OVER0, 8);
}

static void draw_mem_row(struct canvas *c, int x, int y, int w, const char *label,
                         uint64_t used, uint64_t total, uint32_t label_color) {
  cv_text(c, x, y + 4, label, label_color, 8);
  char text[48], a[16], b[16];
  fmt_kb(a, sizeof(a), used);
  fmt_kb(b, sizeof(b), total);
  if (total)
    snprintf(text, sizeof(text), "%s/%s", a, b);
  else
    snprintf(text, sizeof(text), "none");
  int tw = cv_text_width(text, 8);
  int bar_w = w - 36 - tw - 10;
  if (bar_w < 30)
    bar_w = 30;
  int pct = total ? (int)(used * 100 / total) : 0;
  draw_bar(c, x + 36, y + 3, bar_w, 10, pct);
  cv_text(c, x + 36 + bar_w + 8, y + 4, text, C_TEXT, 8);
}

static void sort_arrow(char *label, size_t cap, const char *name, struct ui_state *u, int key) {
  if (u->sort_key == key)
    snprintf(label, cap, "%s%s", name, u->sort_desc ? "v" : "^");
  else
    snprintf(label, cap, "%s", name);
}

static void draw_table(struct ui_state *u, struct canvas *c, int y0, int bottom) {
  static const char *const names[UI_COLUMNS] = {"PID", "PPID", "USER", "S", "CPU%", "MEM", "TIME+", "Command"};
  static const int sort_for[UI_COLUMNS] = {SORT_PID, SORT_PID, -1, -1, SORT_CPU, SORT_MEM, SORT_TIME, SORT_NAME};
  static const int widths[UI_COLUMNS - 1] = {52, 52, 52, 26, 100, 116, 76};
  const struct mon_model *m = &u->model;
  u->hdr_y = y0;
  int x = PAD + 6;
  for (int i = 0; i < UI_COLUMNS; i++) {
    u->col_x[i] = x;
    if (i < UI_COLUMNS - 1)
      x += widths[i];
  }
  u->col_x[UI_COLUMNS] = c->w - PAD;
  cv_rrect(c, PAD, y0, c->w - 2 * PAD, 20, 4, C_SURF0);
  for (int i = 0; i < UI_COLUMNS; i++) {
    char label[24];
    sort_arrow(label, sizeof(label), names[i], u, sort_for[i]);
    bool active = sort_for[i] >= 0 && sort_for[i] == u->sort_key;
    cv_text(c, u->col_x[i], y0 + 6, label, active ? C_BLUE : C_TEXT, 8);
  }
  u->row_y0 = y0 + 24;
  u->row_h = LH;
  u->rows_visible = (bottom - u->row_y0) / LH;
  if (u->rows_visible < 1)
    u->rows_visible = 1;
  if (u->sel < u->scroll)
    u->scroll = u->sel;
  if (u->sel >= u->scroll + u->rows_visible)
    u->scroll = u->sel - u->rows_visible + 1;
  int max_scroll = u->visible > u->rows_visible ? u->visible - u->rows_visible : 0;
  if (u->scroll > max_scroll)
    u->scroll = max_scroll;
  if (u->scroll < 0)
    u->scroll = 0;
  cv_clip(c, 0, u->row_y0, c->w, bottom - u->row_y0);
  for (int r = 0; r < u->rows_visible && u->scroll + r < u->visible; r++) {
    int index = u->scroll + r;
    const struct mon_proc *p = &m->procs[u->order[index]];
    int y = u->row_y0 + r * LH;
    bool selected = index == u->sel;
    uint32_t bg = selected ? C_SURF1 : ((r & 1) ? C_BASE : C_MANTLE);
    cv_fill(c, PAD, y, c->w - 2 * PAD, LH, bg);
    if (selected)
      cv_fill(c, PAD, y, 3, LH, C_BLUE);
    bool dead = p->state == 'Z';
    uint32_t text_color = dead ? C_OVER0 : C_TEXT;
    char cell[48];
    snprintf(cell, sizeof(cell), "%u", p->pid);
    cv_text(c, u->col_x[0], y + 4, cell, dead ? C_OVER0 : C_TEAL, 8);
    snprintf(cell, sizeof(cell), "%u", p->ppid);
    cv_text(c, u->col_x[1], y + 4, cell, C_SUB0, 8);
    if (p->uid == 0)
      snprintf(cell, sizeof(cell), "root");
    else
      snprintf(cell, sizeof(cell), "%u", p->uid);
    cv_text(c, u->col_x[2], y + 4, cell, p->uid == 0 ? C_PEACH : C_SUB0, 8);
    uint32_t state_color = p->state == 'R' ? C_GREEN : (p->state == 'Z' ? C_RED : C_SUB0);
    cell[0] = p->state;
    cell[1] = 0;
    cv_text(c, u->col_x[3], y + 4, cell, state_color, 8);
    snprintf(cell, sizeof(cell), "%3u%%", p->cpu);
    cv_text(c, u->col_x[4], y + 4, cell, dead ? C_OVER0 : pct_color((int)p->cpu), 8);
    cv_rrect(c, u->col_x[4] + 42, y + 5, 46, 6, 2, C_SURF0);
    int fill = 44 * (int)(p->cpu > 100 ? 100 : p->cpu) / 100;
    if (fill > 0)
      cv_rrect(c, u->col_x[4] + 43, y + 6, fill, 4, 1, pct_color((int)p->cpu));
    fmt_kb(cell, sizeof(cell), p->rss_kb);
    cv_text(c, u->col_x[5], y + 4, cell, dead ? C_OVER0 : C_SUB0, 8);
    int mem_pct = m->mem_total_kb ? (int)(p->rss_kb * 100 / m->mem_total_kb) : 0;
    cv_rrect(c, u->col_x[5] + 58, y + 5, 46, 6, 2, C_SURF0);
    fill = 44 * (mem_pct > 100 ? 100 : mem_pct) / 100;
    if (p->rss_kb && fill < 2)
      fill = 2;
    if (fill > 0)
      cv_rrect(c, u->col_x[5] + 59, y + 6, fill, 4, 1, C_BLUE);
    fmt_time(cell, sizeof(cell), p->time_ms);
    cv_text(c, u->col_x[6], y + 4, cell, dead ? C_OVER0 : C_SUB0, 8);
    int cx = u->col_x[7];
    int avail = u->col_x[UI_COLUMNS] - cx - 14;
    if (u->tree && p->depth > 0) {
      int indent = p->depth * 12;
      cv_text(c, cx + indent - 4, y + 4, "|-", C_OVER0, 8);
      cx += indent + 14;
      avail -= indent + 14;
    }
    const char *full = p->cmd[0] ? p->cmd : p->name;
    const char *space = strchr(full, ' ');
    char head[MON_CMD];
    size_t hl = space ? (size_t)(space - full) : strlen(full);
    if (hl >= sizeof(head))
      hl = sizeof(head) - 1;
    memcpy(head, full, hl);
    head[hl] = 0;
    cv_text_fit(c, cx, y + 4, avail, head, p->state == 'R' && !dead ? C_GREEN : text_color, 8);
    if (space) {
      int hw = cv_text_width(head, 8) + 8;
      if (hw < avail)
        cv_text_fit(c, cx + hw, y + 4, avail - hw, space + 1, C_SUB0, 8);
    }
  }
  cv_clip_reset(c);
  if (u->visible > u->rows_visible) {
    int track_y = u->row_y0, track_h = u->rows_visible * LH;
    int thumb_h = track_h * u->rows_visible / u->visible;
    if (thumb_h < 16)
      thumb_h = 16;
    int thumb_y = track_y + (track_h - thumb_h) * u->scroll / (u->visible - u->rows_visible);
    cv_rrect(c, c->w - PAD - 6, track_y, 6, track_h, 3, C_SURF0);
    cv_rrect(c, c->w - PAD - 6, thumb_y, 6, thumb_h, 3, C_SURF2);
  }
  if (u->visible == 0)
    cv_text(c, PAD + 10, u->row_y0 + 8,
            u->filter[0] ? "No process matches the filter" : "No process data in /proc",
            C_OVER0, 8);
}

static void draw_details(struct ui_state *u, struct canvas *c, int y, int h) {
  cv_frame(c, PAD, y, c->w - 2 * PAD, h, 6, C_SURF1, C_MANTLE);
  if (u->sel >= u->visible) {
    cv_text(c, PAD + 12, y + 10, "No process selected", C_OVER0, 8);
    return;
  }
  const struct mon_proc *p = &u->model.procs[u->order[u->sel]];
  char title[96];
  snprintf(title, sizeof(title), "Process %u  %s", p->pid, p->name);
  cv_text(c, PAD + 12, y + 8, title, C_MAUVE, 8);
  cv_fill(c, PAD + 10, y + 22, c->w - 2 * PAD - 20, 1, C_SURF0);
  int col = 0, row = 0;
  int col_w = (c->w - 2 * PAD - 24) / 3;
  const char *line = u->detail;
  while (*line) {
    const char *end = strchr(line, '\n');
    size_t len = end ? (size_t)(end - line) : strlen(line);
    char buf[96];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, line, n);
    buf[n] = 0;
    char *tab = strchr(buf, '\t');
    int x = PAD + 12 + col * col_w;
    int ty = y + 28 + row * 14;
    if (tab) {
      *tab = 0;
      char key[40];
      snprintf(key, sizeof(key), "%s", buf);
      int kw = (int)strlen(key) * 8 + 8;
      cv_text(c, x, ty, key, C_SUB0, 8);
      cv_text_fit(c, x + 88 > x + kw ? x + 88 : x + kw, ty, col_w - 96, tab + 1, C_TEXT, 8);
    } else if (n) {
      cv_text_fit(c, x, ty, col_w - 8, buf, C_TEXT, 8);
    }
    row++;
    if (row >= 6) {
      row = 0;
      col++;
      if (col >= 3)
        break;
    }
    if (!end)
      break;
    line = end + 1;
  }
}

static void draw_processes(struct ui_state *u, struct canvas *c) {
  const struct mon_model *m = &u->model;
  int y = TOP + 12;
  int left_w = (c->w - 3 * PAD) * 56 / 100;
  int cores_h = draw_cores(u, c, PAD, y, left_w);
  int rx = PAD + left_w + PAD;
  int rw = c->w - rx - PAD;
  draw_mem_row(c, rx, y, rw, "Mem", m->mem_used_kb, m->mem_total_kb, C_MAUVE);
  draw_mem_row(c, rx, y + LH, rw, "Swp", m->swap_total_kb - m->swap_free_kb, m->swap_total_kb, C_PEACH);
  int graph_y = y + 2 * LH + 8;
  int dash_h = cores_h > 2 * LH + 52 ? cores_h : 2 * LH + 52;
  int graph_h = y + dash_h - graph_y;
  if (graph_h < 34)
    graph_h = 34;
  draw_history(u, c, rx, graph_y, rw, graph_h);
  int table_y = y + (dash_h > cores_h ? dash_h : cores_h) + 12;
  int bottom = c->h - FOOT - 6;
  if (u->details)
    bottom -= DETAIL_H + 6;
  draw_table(u, c, table_y, bottom);
  if (u->details)
    draw_details(u, c, c->h - FOOT - 6 - DETAIL_H, DETAIL_H);
}

struct card {
  int x, y, w, h;
};

static struct card card_begin(struct canvas *c, int x, int y, int w, int h, const char *title) {
  struct card k = {x, y, w, h};
  cv_frame(c, x, y, w, h, 8, C_SURF0, C_MANTLE);
  cv_text(c, x + 14, y + 10, title, C_MAUVE, 8);
  cv_fill(c, x + 12, y + 24, w - 24, 1, C_SURF0);
  return k;
}

static void kv(struct canvas *c, int x, int y, int key_w, const char *key, const char *value, int vmax) {
  cv_text(c, x, y, key, C_SUB0, 8);
  cv_text_fit(c, x + key_w, y, vmax - key_w, value, C_TEXT, 8);
}

static int draw_system(struct ui_state *u, struct canvas *c) {
  const struct mon_model *m = &u->model;
  int area_y = TOP, area_h = c->h - TOP - FOOT;
  cv_clip(c, 0, area_y, c->w, area_h);
  int colw = (c->w - 3 * PAD) / 2;
  int lx = PAD, rx = PAD + colw + PAD;
  int ly = area_y + 12 - u->sys_scroll;
  int ry = ly;
  char buf[160], a[24], b[24];

  int kh = 36 + 3 * LH + 8;
  card_begin(c, lx, ly, colw, kh, "Kernel");
  int ty = ly + 34;
  kv(c, lx + 14, ty, 96, "Version", m->version[0] ? m->version : "-", colw - 28);
  fmt_uptime(a, sizeof(a), m->uptime_cs);
  kv(c, lx + 14, ty + LH, 96, "Uptime", a, colw - 28);
  snprintf(buf, sizeof(buf), "%d total, %d running", m->tasks_total, m->tasks_running);
  kv(c, lx + 14, ty + 2 * LH, 96, "Tasks", buf, colw - 28);
  ly += kh + 12;

  int n = m->cpu_count;
  int cols = n > 4 ? 2 : 1;
  int rows = n ? (n + cols - 1) / cols : 1;
  int ch = 36 + 2 * LH + rows * LH + 8 + 60 + 12;
  card_begin(c, lx, ly, colw, ch, "CPU");
  ty = ly + 34;
  kv(c, lx + 14, ty, 96, "Model", m->cpu_model[0] ? m->cpu_model : "unknown", colw - 28);
  if (m->cpu_mhz)
    snprintf(buf, sizeof(buf), "%u MHz    %d of %d online", m->cpu_mhz, m->cpus_online, n);
  else
    snprintf(buf, sizeof(buf), "n/a    %d of %d online", m->cpus_online, n);
  kv(c, lx + 14, ty + LH, 96, "Clock", buf, colw - 28);
  int by = ty + 2 * LH + 6;
  int bw = (colw - 28 - (cols - 1) * 16) / cols;
  for (int i = 0; i < n; i++) {
    int col = i / rows, row = i % rows;
    int bx = lx + 14 + col * (bw + 16);
    int yy = by + row * LH;
    snprintf(a, sizeof(a), "%2d", i);
    cv_text(c, bx, yy + 4, a, C_TEAL, 8);
    int barw = bw - 24 - 44;
    if (!m->cpu[i].online) {
      cv_rrect(c, bx + 24, yy + 3, barw, 10, 3, C_SURF0);
      cv_text(c, bx + 24 + barw + 6, yy + 4, "off", C_OVER0, 8);
    } else {
      draw_bar(c, bx + 24, yy + 3, barw, 10, (int)m->cpu[i].pct);
      snprintf(a, sizeof(a), "%3u%%", m->cpu[i].pct);
      cv_text(c, bx + 24 + barw + 6, yy + 4, a, pct_color((int)m->cpu[i].pct), 8);
    }
  }
  draw_history(u, c, lx + 14, by + rows * LH + 8, colw - 28, 60);
  ly += ch + 12;

  int mh = 36 + 4 * LH + 8;
  card_begin(c, lx, ly, colw, mh, "Memory");
  ty = ly + 34;
  draw_mem_row(c, lx + 14, ty, colw - 28, "RAM", m->mem_used_kb, m->mem_total_kb, C_MAUVE);
  draw_mem_row(c, lx + 14, ty + LH, colw - 28, "Swap", m->swap_total_kb - m->swap_free_kb,
               m->swap_total_kb, C_PEACH);
  fmt_kb(a, sizeof(a), m->mem_free_kb);
  fmt_kb(b, sizeof(b), m->mem_total_kb);
  snprintf(buf, sizeof(buf), "%s free of %s", a, b);
  kv(c, lx + 14, ty + 2 * LH + 6, 96, "Free", buf, colw - 28);
  int mem_pct = m->mem_total_kb ? (int)(m->mem_used_kb * 100 / m->mem_total_kb) : 0;
  snprintf(buf, sizeof(buf), "%d%%", mem_pct);
  kv(c, lx + 14, ty + 3 * LH + 6, 96, "Used", buf, colw - 28);
  ly += mh + 12;

  int dh = 36 + (m->disk_count + 1) * LH + 8;
  card_begin(c, rx, ry, colw, dh, "Block devices");
  ty = ry + 34;
  cv_text(c, rx + 14, ty, "NAME", C_OVER0, 8);
  cv_text(c, rx + 94, ty, "SIZE", C_OVER0, 8);
  cv_text(c, rx + 158, ty, "MODEL", C_OVER0, 8);
  for (int i = 0; i < m->disk_count; i++) {
    int yy = ty + (i + 1) * LH;
    fmt_size(a, sizeof(a), m->disks[i].sectors, m->disks[i].sector_size);
    cv_text(c, rx + 14, yy, m->disks[i].name, C_TEAL, 8);
    cv_text(c, rx + 94, yy, a, C_TEXT, 8);
    cv_text_fit(c, rx + 158, yy, colw - 172, m->disks[i].model, C_SUB0, 8);
  }
  ry += dh + 12;

  int mounth = 36 + (m->mount_count + 1) * LH + 8;
  card_begin(c, rx, ry, colw, mounth, "Mounts");
  ty = ry + 34;
  cv_text(c, rx + 14, ty, "SOURCE", C_OVER0, 8);
  cv_text(c, rx + 110, ty, "PATH", C_OVER0, 8);
  cv_text(c, rx + 214, ty, "TYPE", C_OVER0, 8);
  cv_text(c, rx + 294, ty, "MODE", C_OVER0, 8);
  for (int i = 0; i < m->mount_count; i++) {
    int yy = ty + (i + 1) * LH;
    cv_text_fit(c, rx + 14, yy, 90, m->mounts[i].source, C_TEXT, 8);
    cv_text_fit(c, rx + 110, yy, 98, m->mounts[i].path, C_BLUE, 8);
    cv_text_fit(c, rx + 214, yy, 74, m->mounts[i].type, C_SUB0, 8);
    cv_text(c, rx + 294, yy, m->mounts[i].opts, m->mounts[i].opts[0] == 'r' && m->mounts[i].opts[1] == 'o' ? C_YELLOW : C_GREEN, 8);
  }
  ry += mounth + 12;

  int ih = 36 + (m->irq_count ? m->irq_count : 1) * LH + 8;
  card_begin(c, rx, ry, colw, ih, "Interrupts (top vectors)");
  ty = ry + 34;
  uint64_t top = m->irq_count ? m->irqs[0].count : 1;
  for (int i = 0; i < m->irq_count; i++) {
    int yy = ty + i * LH;
    snprintf(a, sizeof(a), "%3u", m->irqs[i].vector);
    cv_text(c, rx + 14, yy + 4, a, C_TEAL, 8);
    int barw = colw - 14 - 40 - 130;
    int fill = top ? (int)(m->irqs[i].count * (uint64_t)barw / top) : 0;
    cv_rrect(c, rx + 48, yy + 3, barw, 10, 3, C_SURF0);
    if (fill > 1)
      cv_rrect(c, rx + 48, yy + 3, fill, 10, 3, C_BLUE);
    snprintf(a, sizeof(a), "%llu", (unsigned long long)m->irqs[i].count);
    cv_text(c, rx + 48 + barw + 8, yy + 4, a, C_TEXT, 8);
  }
  if (!m->irq_count)
    cv_text(c, rx + 14, ty + 4, "no data", C_OVER0, 8);
  ry += ih + 12;

  int fh = 36 + 2 * LH + 8;
  card_begin(c, rx, ry, colw, fh, "Filesystems");
  cv_text_fit(c, rx + 14, ry + 36, colw - 28, m->filesystems[0] ? m->filesystems : "-", C_TEXT, 8);
  ry += fh + 12;

  int content = (ly > ry ? ly : ry) - (area_y - u->sys_scroll) + 4;
  cv_clip_reset(c);
  return content;
}

static void draw_proc_tab(struct ui_state *u, struct canvas *c) {
  int y = TOP + 12;
  int h = c->h - y - FOOT - 8;
  u->list_x = PAD;
  u->list_y = y;
  u->list_w = LIST_W;
  u->list_rows = (h - 36) / LH;
  cv_frame(c, PAD, y, LIST_W, h, 8, C_SURF0, C_MANTLE);
  cv_text(c, PAD + 14, y + 10, "/proc", C_MAUVE, 8);
  cv_fill(c, PAD + 12, y + 24, LIST_W - 24, 1, C_SURF0);
  if (u->browse_sel < u->browse_scroll)
    u->browse_scroll = u->browse_sel;
  if (u->browse_sel >= u->browse_scroll + u->list_rows)
    u->browse_scroll = u->browse_sel - u->list_rows + 1;
  if (u->browse_scroll < 0)
    u->browse_scroll = 0;
  cv_clip(c, PAD, y + 28, LIST_W, h - 32);
  for (int r = 0; r < u->list_rows && u->browse_scroll + r < u->browse_count; r++) {
    int index = u->browse_scroll + r;
    const struct mon_entry *e = &u->browse[index];
    int yy = y + 32 + r * LH;
    bool selected = index == u->browse_sel;
    if (selected) {
      cv_rrect(c, PAD + 6, yy, LIST_W - 12, LH, 4, C_SURF1);
      cv_fill(c, PAD + 6, yy + 3, 3, LH - 6, C_BLUE);
    }
    char label[64];
    uint32_t color = e->is_dir ? C_PEACH : C_TEXT;
    if (e->is_dir) {
      uint32_t pid = (uint32_t)strtoul(e->name, 0, 10);
      const char *name = "";
      for (int k = 0; k < u->model.proc_count; k++)
        if (u->model.procs[k].pid == pid)
          name = u->model.procs[k].name;
      snprintf(label, sizeof(label), "%s/  %s", e->name, name);
    } else {
      snprintf(label, sizeof(label), "%s", e->name);
      color = C_TEXT;
    }
    cv_text_fit(c, PAD + 16, yy + 4, LIST_W - 32, label, selected ? C_BLUE : color, 8);
  }
  cv_clip_reset(c);
  int px = PAD + LIST_W + 12;
  int pw = c->w - px - PAD;
  u->content_x = px;
  cv_frame(c, px, y, pw, h, 8, C_SURF0, C_MANTLE);
  char title[96] = "(nothing selected)";
  if (u->browse_sel >= 0 && u->browse_sel < u->browse_count)
    snprintf(title, sizeof(title), "/proc/%s%s", u->browse[u->browse_sel].name,
             u->browse[u->browse_sel].is_dir ? "/" : "");
  cv_text(c, px + 14, y + 10, title, C_MAUVE, 8);
  char meta[48];
  snprintf(meta, sizeof(meta), "%d lines", u->line_count);
  cv_text(c, right_align_x(meta, px + pw - 14), y + 10, meta, C_OVER0, 8);
  cv_fill(c, px + 12, y + 24, pw - 24, 1, C_SURF0);
  int rows = (h - 36) / LH;
  int max_scroll = u->line_count > rows ? u->line_count - rows : 0;
  if (u->text_scroll > max_scroll)
    u->text_scroll = max_scroll;
  if (u->text_scroll < 0)
    u->text_scroll = 0;
  cv_clip(c, px + 4, y + 28, pw - 8, h - 32);
  for (int r = 0; r < rows && u->text_scroll + r < u->line_count; r++) {
    int ls = u->line_start[u->text_scroll + r];
    int le = ls;
    while (le < u->text_len && u->text[le] != '\n')
      le++;
    char line[200];
    int n = le - ls;
    if (n > (int)sizeof(line) - 1)
      n = (int)sizeof(line) - 1;
    memcpy(line, u->text + ls, (size_t)n);
    line[n] = 0;
    uint32_t color = C_TEXT;
    if (!strncmp(line, "==>", 3))
      color = C_PEACH;
    else {
      char *colon = strchr(line, ':');
      if (colon && colon - line < 20) {
        char key[24];
        int kl = (int)(colon - line) + 1;
        memcpy(key, line, (size_t)kl);
        key[kl] = 0;
        cv_text(c, px + 14, y + 32 + r * LH + 4, key, C_BLUE, 8);
        cv_text_fit(c, px + 14 + kl * 8, y + 32 + r * LH + 4, pw - 40 - kl * 8, colon + 1, C_TEXT, 8);
        continue;
      }
    }
    cv_text_fit(c, px + 14, y + 32 + r * LH + 4, pw - 40, line, color, 8);
  }
  cv_clip_reset(c);
  if (u->line_count > rows) {
    int track_h = rows * LH;
    int thumb_h = track_h * rows / u->line_count;
    if (thumb_h < 16)
      thumb_h = 16;
    int thumb_y = y + 32 + (track_h - thumb_h) * u->text_scroll / (u->line_count - rows);
    cv_rrect(c, px + pw - 12, y + 32, 6, track_h, 3, C_SURF0);
    cv_rrect(c, px + pw - 12, thumb_y, 6, thumb_h, 3, C_SURF2);
  }
}

static void draw_kill_dialog(struct ui_state *u, struct canvas *c) {
  int w = 420, h = 100;
  int x = (c->w - w) / 2, y = (c->h - h) / 2;
  cv_frame(c, x, y, w, h, 10, C_RED, C_MANTLE);
  const char *name = "?";
  for (int i = 0; i < u->model.proc_count; i++)
    if (u->model.procs[i].pid == u->kill_pid)
      name = u->model.procs[i].name;
  char line[96];
  snprintf(line, sizeof(line), "Send SIGTERM to PID %u (%s)?", u->kill_pid, name);
  cv_text(c, x + 24, y + 22, line, C_TEXT, 8);
  cv_rrect(c, x + 24, y + 58, 136, 24, 5, C_RED);
  cv_text(c, x + 36, y + 66, "[y] terminate", C_CRUST, 8);
  cv_rrect(c, x + 176, y + 58, 112, 24, 5, C_SURF1);
  cv_text(c, x + 188, y + 66, "[n] cancel", C_TEXT, 8);
}

void ui_render(struct ui_state *u, struct canvas *c) {
  u->width = c->w;
  u->height = c->h;
  cv_clip_reset(c);
  cv_fill(c, 0, 0, c->w, c->h, C_BASE);
  switch (u->tab) {
  case TAB_PROCS:
    draw_processes(u, c);
    break;
  case TAB_SYSTEM: {
    int content = draw_system(u, c);
    u->sys_content_h = content;
    int area = c->h - TOP - FOOT;
    int max_scroll = content > area ? content - area : 0;
    if (u->sys_scroll > max_scroll)
      u->sys_scroll = max_scroll;
    break;
  }
  default:
    draw_proc_tab(u, c);
    break;
  }
  draw_topbar(u, c);
  draw_footer(u, c);
  if (u->kill_prompt)
    draw_kill_dialog(u, c);
}

static void select_index(struct ui_state *u, int index) {
  if (u->visible <= 0)
    return;
  if (index < 0)
    index = 0;
  if (index >= u->visible)
    index = u->visible - 1;
  u->sel = index;
  u->sel_pid = u->model.procs[u->order[index]].pid;
  if (u->details)
    load_detail(u);
}

static void set_tab(struct ui_state *u, int tab) {
  u->tab = tab;
  if (tab == TAB_PROC)
    refresh_browse(u);
}

static void set_sort(struct ui_state *u, int key) {
  if (u->sort_key == key)
    u->sort_desc = !u->sort_desc;
  else {
    u->sort_key = key;
    u->sort_desc = key == SORT_CPU || key == SORT_MEM || key == SORT_TIME;
  }
  u->tree = false;
  rebuild_order(u);
}

bool ui_key(struct ui_state *u, int key) {
  if (u->kill_prompt) {
    if (key == 'y' || key == 'Y') {
      char msg[96];
      int rc = u->kill_hook ? u->kill_hook(u->kill_pid) : -1;
      if (rc >= 0)
        snprintf(msg, sizeof(msg), "Sent SIGTERM to PID %u", u->kill_pid);
      else
        snprintf(msg, sizeof(msg), "Cannot signal PID %u (error %d)", u->kill_pid, rc);
      set_message(u, msg);
      u->kill_prompt = false;
    } else if (key == 'n' || key == 'N' || key == 27 || key == 'q') {
      u->kill_prompt = false;
    }
    return true;
  }
  if (u->filter_mode) {
    if (key == 10 || key == 13) {
      u->filter_mode = false;
    } else if (key == 27) {
      u->filter_mode = false;
      u->filter[0] = 0;
      u->filter_len = 0;
    } else if ((key == 8 || key == 127) && u->filter_len > 0) {
      u->filter[--u->filter_len] = 0;
    } else if (key >= 32 && key < 127 && u->filter_len < (int)sizeof(u->filter) - 1) {
      u->filter[u->filter_len++] = (char)key;
      u->filter[u->filter_len] = 0;
    }
    rebuild_order(u);
    return true;
  }
  switch (key) {
  case '1': set_tab(u, TAB_PROCS); return true;
  case '2': set_tab(u, TAB_SYSTEM); return true;
  case '3': set_tab(u, TAB_PROC); return true;
  case 9: set_tab(u, (u->tab + 1) % TAB_COUNT); return true;
  case 'q':
  case 'Q': u->quit = true; return false;
  case 'r':
  case 'R': ui_refresh(u); return true;
  default: break;
  }
  if (u->tab != TAB_PROCS)
    return false;
  switch (key) {
  case 'c': set_sort(u, SORT_CPU); return true;
  case 'm': set_sort(u, SORT_MEM); return true;
  case 'p': set_sort(u, SORT_PID); return true;
  case 'n': set_sort(u, SORT_NAME); return true;
  case 'T': set_sort(u, SORT_TIME); return true;
  case 't':
    u->tree = !u->tree;
    rebuild_order(u);
    return true;
  case '/':
    u->filter_mode = true;
    return true;
  case 27:
    if (u->details) {
      u->details = false;
      return true;
    }
    if (u->filter[0]) {
      u->filter[0] = 0;
      u->filter_len = 0;
      rebuild_order(u);
      return true;
    }
    return false;
  case 10:
  case 13:
    u->details = !u->details;
    load_detail(u);
    return true;
  case 'k':
  case 'K':
    if (u->sel < u->visible) {
      uint32_t pid = u->model.procs[u->order[u->sel]].pid;
      if (pid == 1) {
        set_message(u, "Refusing to signal PID 1");
      } else {
        u->kill_prompt = true;
        u->kill_pid = pid;
      }
      return true;
    }
    return false;
  default:
    return false;
  }
}

bool ui_special(struct ui_state *u, int code) {
  if (u->kill_prompt || u->filter_mode)
    return false;
  int page = u->rows_visible > 1 ? u->rows_visible - 1 : 1;
  if (u->tab == TAB_PROCS) {
    switch (code) {
    case 10: select_index(u, u->sel - 1); return true;
    case 11: select_index(u, u->sel + 1); return true;
    case 6: select_index(u, u->sel - page); return true;
    case 7: select_index(u, u->sel + page); return true;
    case 4: select_index(u, 0); return true;
    case 5: select_index(u, u->visible - 1); return true;
    case 12: return false;
    default: return false;
    }
  }
  if (u->tab == TAB_SYSTEM) {
    switch (code) {
    case 10: u->sys_scroll -= 32; break;
    case 11: u->sys_scroll += 32; break;
    case 6: u->sys_scroll -= 240; break;
    case 7: u->sys_scroll += 240; break;
    case 4: u->sys_scroll = 0; break;
    case 5: u->sys_scroll = 1 << 20; break;
    default: return false;
    }
    if (u->sys_scroll < 0)
      u->sys_scroll = 0;
    return true;
  }
  switch (code) {
  case 10:
    if (u->browse_sel > 0) {
      u->browse_sel--;
      u->text_scroll = 0;
      load_browse_text(u);
    }
    return true;
  case 11:
    if (u->browse_sel + 1 < u->browse_count) {
      u->browse_sel++;
      u->text_scroll = 0;
      load_browse_text(u);
    }
    return true;
  case 6: u->text_scroll -= 12; return true;
  case 7: u->text_scroll += 12; return true;
  case 4: u->text_scroll = 0; return true;
  case 5: u->text_scroll = 1 << 20; return true;
  default: return false;
  }
}

bool ui_click(struct ui_state *u, int x, int y) {
  if (u->kill_prompt)
    return false;
  if (y < TOP) {
    for (int i = 0; i < TAB_COUNT; i++)
      if (x >= u->tab_x[i] && x < u->tab_x[i + 1] - 4) {
        set_tab(u, i);
        return true;
      }
    return false;
  }
  if (u->tab == TAB_PROCS) {
    if (y >= u->hdr_y && y < u->hdr_y + 20) {
      static const int sort_for[UI_COLUMNS] = {SORT_PID, SORT_PID, -1, -1, SORT_CPU, SORT_MEM, SORT_TIME, SORT_NAME};
      for (int i = 0; i < UI_COLUMNS; i++)
        if (x >= u->col_x[i] - 4 && x < u->col_x[i + 1] - 4 && sort_for[i] >= 0) {
          set_sort(u, sort_for[i]);
          return true;
        }
      return false;
    }
    if (y >= u->row_y0 && y < u->row_y0 + u->rows_visible * LH) {
      int index = u->scroll + (y - u->row_y0) / LH;
      if (index < u->visible) {
        if (index == u->sel)
          u->details = !u->details;
        select_index(u, index);
        if (u->details)
          load_detail(u);
        return true;
      }
    }
    return false;
  }
  if (u->tab == TAB_PROC) {
    if (x >= u->list_x && x < u->list_x + u->list_w && y >= u->list_y + 32 &&
        y < u->list_y + 32 + u->list_rows * LH) {
      int index = u->browse_scroll + (y - u->list_y - 32) / LH;
      if (index < u->browse_count) {
        u->browse_sel = index;
        u->text_scroll = 0;
        load_browse_text(u);
        return true;
      }
    }
  }
  return false;
}

bool ui_wheel(struct ui_state *u, int delta, int x, int y) {
  (void)y;
  if (u->kill_prompt || !delta)
    return false;
  if (u->tab == TAB_PROCS) {
    int max_scroll = u->visible > u->rows_visible ? u->visible - u->rows_visible : 0;
    int next = u->scroll - delta * 3;
    if (next < 0)
      next = 0;
    if (next > max_scroll)
      next = max_scroll;
    u->scroll = next;
    return true;
  }
  if (u->tab == TAB_SYSTEM) {
    u->sys_scroll -= delta * 36;
    if (u->sys_scroll < 0)
      u->sys_scroll = 0;
    return true;
  }
  if (x < u->list_x + u->list_w) {
    u->browse_scroll -= delta * 3;
    int max = u->browse_count > u->list_rows ? u->browse_count - u->list_rows : 0;
    if (u->browse_scroll < 0)
      u->browse_scroll = 0;
    if (u->browse_scroll > max)
      u->browse_scroll = max;
  } else {
    u->text_scroll -= delta * 3;
  }
  return true;
}
