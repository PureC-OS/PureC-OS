#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_DIR "/var/logs"
#define MAX_BOOTS 256
#define LINE_CAPACITY 2048
#define MAX_FILES 16

struct record {
  unsigned boot;
  unsigned long long unix_time;
  unsigned long long uptime_us;
  char level;
  int cpu;
  const char *message;
};

struct boot_info {
  unsigned id;
  unsigned long long first_unix;
  unsigned long long last_unix;
  unsigned long long first_us;
  unsigned long long last_us;
};

struct options {
  const char *directory;
  int boot_selected;
  long boot_offset;
  unsigned boot_id;
  bool boot_by_id;
  int min_rank;
  long tail;
  const char *grep;
  unsigned long long since;
  unsigned long long until;
  int output;
  bool follow;
  bool list_boots;
};

enum { OUTPUT_SHORT, OUTPUT_MONOTONIC, OUTPUT_CAT };

static struct boot_info boots[MAX_BOOTS];
static int boot_count;
static struct options opt;
static unsigned selected_boot;
static bool selected_boot_valid;

static int level_rank(char level) {
  switch (level) {
  case 'E': return 3;
  case 'W': return 2;
  case 'D': return 0;
  default: return 1;
  }
}

static const char *level_tag(char level) {
  switch (level) {
  case 'E': return "FAIL";
  case 'W': return "WARN";
  case 'O': return "OK";
  case 'D': return "DEBUG";
  default: return "INFO";
  }
}

static bool parse_record(char *line, struct record *r) {
  size_t length = strlen(line);
  while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
    line[--length] = 0;
  if (length < 20)
    return false;
  char *cursor = line;
  char *end;
  r->boot = (unsigned)strtoul(cursor, &end, 16);
  if (end - cursor != 8 || *end != ' ')
    return false;
  cursor = end + 1;
  r->unix_time = strtoull(cursor, &end, 10);
  if (end == cursor || *end != ' ')
    return false;
  cursor = end + 1;
  r->uptime_us = strtoull(cursor, &end, 10);
  if (end == cursor || *end != ' ')
    return false;
  cursor = end + 1;
  r->level = *cursor;
  if (!r->level || cursor[1] != ' ')
    return false;
  cursor += 2;
  if (*cursor == '-') {
    r->cpu = -1;
    end = cursor + 1;
  } else {
    r->cpu = (int)strtol(cursor, &end, 10);
    if (end == cursor)
      return false;
  }
  if (*end != ' ')
    return false;
  r->message = end + 1;
  return true;
}

static int file_list(char names[MAX_FILES][512]) {
  int count = 0;
  for (int index = MAX_FILES - 2; index >= 1; index--) {
    char path[512];
    snprintf(path, sizeof(path), "%s/journal.%d.log", opt.directory, index);
    FILE *f = fopen(path, "rb");
    if (!f)
      continue;
    fclose(f);
    strcpy(names[count++], path);
  }
  snprintf(names[count], 512, "%s/journal.log", opt.directory);
  FILE *f = fopen(names[count], "rb");
  if (f) {
    fclose(f);
    count++;
  }
  return count;
}

typedef void (*visit_fn)(const struct record *r, void *context);

static void walk(visit_fn visit, void *context) {
  static char names[MAX_FILES][512];
  int files = file_list(names);
  for (int i = 0; i < files; i++) {
    FILE *f = fopen(names[i], "rb");
    if (!f)
      continue;
    char line[LINE_CAPACITY];
    while (fgets(line, sizeof(line), f)) {
      struct record r;
      if (parse_record(line, &r))
        visit(&r, context);
    }
    fclose(f);
  }
}

static void collect_boot(const struct record *r, void *context) {
  (void)context;
  int index = -1;
  for (int i = boot_count - 1; i >= 0; i--) {
    if (boots[i].id == r->boot) {
      index = i;
      break;
    }
  }
  if (index < 0) {
    if (boot_count == MAX_BOOTS) {
      memmove(boots, boots + 1, sizeof(boots[0]) * (MAX_BOOTS - 1));
      boot_count--;
    }
    index = boot_count++;
    boots[index].id = r->boot;
    boots[index].first_unix = r->unix_time;
    boots[index].first_us = r->uptime_us;
  }
  boots[index].last_unix = r->unix_time;
  boots[index].last_us = r->uptime_us;
}

static void format_time(unsigned long long unix_time, char *out, size_t capacity) {
  time_t value = (time_t)unix_time;
  struct tm *parts = localtime(&value);
  if (!parts || !unix_time) {
    snprintf(out, capacity, "-");
    return;
  }
  strftime(out, capacity, "%a %Y-%m-%d %H:%M:%S", parts);
}

static void print_boots(void) {
  for (int i = 0; i < boot_count; i++) {
    char first[64], last[64];
    format_time(boots[i].first_unix, first, sizeof(first));
    format_time(boots[i].last_unix, last, sizeof(last));
    printf("%3d %08x %s - %s\n", i - (boot_count - 1), boots[i].id, first, last);
  }
}

static bool matches(const struct record *r) {
  if (selected_boot_valid && r->boot != selected_boot)
    return false;
  if (level_rank(r->level) < opt.min_rank)
    return false;
  if (opt.since && r->unix_time && r->unix_time < opt.since)
    return false;
  if (opt.until && r->unix_time && r->unix_time > opt.until)
    return false;
  if (opt.grep && !strstr(r->message, opt.grep))
    return false;
  return true;
}

struct printer {
  long skip;
  long counted;
  unsigned last_boot;
  bool any;
};

static void print_record(const struct record *r, struct printer *p) {
  if (!p->any || p->last_boot != r->boot) {
    if (opt.output != OUTPUT_CAT)
      printf("-- Boot %08x --\n", r->boot);
    p->last_boot = r->boot;
    p->any = true;
  }
  if (r->level == 'M') {
    printf("-- %s --\n", r->message);
    return;
  }
  if (opt.output == OUTPUT_CAT) {
    printf("%s\n", r->message);
    return;
  }
  char source[24];
  if (r->cpu >= 0)
    snprintf(source, sizeof(source), "kernel[cpu%d]", r->cpu);
  else
    snprintf(source, sizeof(source), "kernel");
  if (opt.output == OUTPUT_SHORT && r->unix_time) {
    time_t value = (time_t)r->unix_time;
    struct tm *parts = localtime(&value);
    char stamp[32];
    if (parts)
      strftime(stamp, sizeof(stamp), "%b %d %H:%M:%S", parts);
    else
      snprintf(stamp, sizeof(stamp), "%llu", r->unix_time);
    printf("%s purec %s: [%s] %s\n", stamp, source, level_tag(r->level), r->message);
  } else {
    printf("[%5llu.%06llu] %s: [%s] %s\n", r->uptime_us / 1000000ull,
           r->uptime_us % 1000000ull, source, level_tag(r->level), r->message);
  }
}

static void count_visit(const struct record *r, void *context) {
  struct printer *p = context;
  if (matches(r))
    p->counted++;
}

static void print_visit(const struct record *r, void *context) {
  struct printer *p = context;
  if (!matches(r))
    return;
  if (p->skip > 0) {
    p->skip--;
    return;
  }
  print_record(r, p);
}

static bool resolve_boot(void) {
  if (!opt.boot_selected)
    return true;
  if (!boot_count)
    return false;
  if (opt.boot_by_id) {
    for (int i = 0; i < boot_count; i++) {
      if (boots[i].id == opt.boot_id) {
        selected_boot = boots[i].id;
        selected_boot_valid = true;
        return true;
      }
    }
    return false;
  }
  long index;
  if (opt.boot_offset > 0)
    index = opt.boot_offset - 1;
  else
    index = (long)boot_count - 1 + opt.boot_offset;
  if (index < 0 || index >= boot_count)
    return false;
  selected_boot = boots[index].id;
  selected_boot_valid = true;
  return true;
}

static unsigned long long parse_when(const char *text, bool *ok) {
  char *end;
  unsigned long long n = strtoull(text, &end, 10);
  *ok = end != text;
  if (!*ok)
    return 0;
  unsigned long long unit = 0;
  if (*end == 's' || *end == 0)
    unit = 1;
  else if (*end == 'm')
    unit = 60;
  else if (*end == 'h')
    unit = 3600;
  else if (*end == 'd')
    unit = 86400;
  else {
    *ok = false;
    return 0;
  }
  if (*end == 0 && n > 1000000000ull)
    return n;
  unsigned long long now = (unsigned long long)time(0);
  unsigned long long delta = n * unit;
  return now > delta ? now - delta : 0;
}

static int rank_from_name(const char *name) {
  if (!strcmp(name, "err") || !strcmp(name, "error") || !strcmp(name, "fail") || !strcmp(name, "3"))
    return 3;
  if (!strcmp(name, "warn") || !strcmp(name, "warning") || !strcmp(name, "2"))
    return 2;
  if (!strcmp(name, "info") || !strcmp(name, "ok") || !strcmp(name, "1"))
    return 1;
  if (!strcmp(name, "debug") || !strcmp(name, "0"))
    return 0;
  return -1;
}

static void usage(void) {
  puts("usage: journalctl [options]");
  puts("  -b [N|ID]      show one boot: no argument/0 = current, -1 = previous, 1 = first");
  puts("  --list-boots   list recorded boots");
  puts("  -p LEVEL       minimum level: debug, info, warn, err");
  puts("  -n N           show only the last N entries");
  puts("  -g TEXT        only entries whose message contains TEXT");
  puts("  --since T      T is 90s, 10m, 2h, 1d (that long ago) or a unix time");
  puts("  --until T      same format");
  puts("  -o FORMAT      short, short-monotonic or cat");
  puts("  -f             follow new entries");
  puts("  -D DIR         journal directory (default " DEFAULT_DIR ")");
}

static bool looks_like_boot_argument(const char *text) {
  if (!*text)
    return false;
  if (*text == '-' || *text == '+')
    text++;
  if (!*text)
    return false;
  for (const char *c = text; *c; c++) {
    bool digit = *c >= '0' && *c <= '9';
    bool hex = (*c >= 'a' && *c <= 'f') || (*c >= 'A' && *c <= 'F');
    if (!digit && !hex)
      return false;
  }
  return true;
}

static int parse_arguments(int argc, char **argv) {
  opt.directory = DEFAULT_DIR;
  opt.tail = -1;
  opt.min_rank = 0;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
      usage();
      return 1;
    } else if (!strcmp(a, "-b") || !strcmp(a, "--boot")) {
      opt.boot_selected = 1;
      opt.boot_offset = 0;
      if (i + 1 < argc && looks_like_boot_argument(argv[i + 1])) {
        const char *v = argv[++i];
        size_t length = strlen(v);
        bool numeric = true;
        for (const char *c = v + (*v == '-' || *v == '+'); *c; c++)
          if (*c < '0' || *c > '9')
            numeric = false;
        if (length == 8 && !(*v == '-' || *v == '+')) {
          opt.boot_by_id = true;
          opt.boot_id = (unsigned)strtoul(v, 0, 16);
        } else if (numeric) {
          opt.boot_offset = strtol(v, 0, 10);
        } else {
          fprintf(stderr, "journalctl: bad boot selector '%s'\n", v);
          return 2;
        }
      }
    } else if (!strcmp(a, "--list-boots")) {
      opt.list_boots = true;
    } else if (!strcmp(a, "-p") || !strcmp(a, "--priority")) {
      if (i + 1 >= argc || (opt.min_rank = rank_from_name(argv[++i])) < 0) {
        fprintf(stderr, "journalctl: -p needs debug, info, warn or err\n");
        return 2;
      }
    } else if (!strcmp(a, "-n") || !strcmp(a, "--lines")) {
      if (i + 1 >= argc) {
        fprintf(stderr, "journalctl: -n needs a number\n");
        return 2;
      }
      opt.tail = strtol(argv[++i], 0, 10);
    } else if (!strcmp(a, "-g") || !strcmp(a, "--grep")) {
      if (i + 1 >= argc) {
        fprintf(stderr, "journalctl: -g needs text\n");
        return 2;
      }
      opt.grep = argv[++i];
    } else if (!strcmp(a, "--since") || !strcmp(a, "--until")) {
      bool ok = false;
      if (i + 1 >= argc) {
        fprintf(stderr, "journalctl: %s needs a time\n", a);
        return 2;
      }
      unsigned long long when = parse_when(argv[++i], &ok);
      if (!ok) {
        fprintf(stderr, "journalctl: bad time '%s'\n", argv[i]);
        return 2;
      }
      if (!strcmp(a, "--since"))
        opt.since = when;
      else
        opt.until = when;
    } else if (!strcmp(a, "-o") || !strcmp(a, "--output")) {
      if (i + 1 >= argc) {
        fprintf(stderr, "journalctl: -o needs a format\n");
        return 2;
      }
      const char *v = argv[++i];
      if (!strcmp(v, "short"))
        opt.output = OUTPUT_SHORT;
      else if (!strcmp(v, "short-monotonic"))
        opt.output = OUTPUT_MONOTONIC;
      else if (!strcmp(v, "cat"))
        opt.output = OUTPUT_CAT;
      else {
        fprintf(stderr, "journalctl: unknown format '%s'\n", v);
        return 2;
      }
    } else if (!strcmp(a, "-f") || !strcmp(a, "--follow")) {
      opt.follow = true;
    } else if (!strcmp(a, "-D") || !strcmp(a, "--directory")) {
      if (i + 1 >= argc) {
        fprintf(stderr, "journalctl: -D needs a directory\n");
        return 2;
      }
      opt.directory = argv[++i];
    } else {
      fprintf(stderr, "journalctl: unknown option '%s' (try --help)\n", a);
      return 2;
    }
  }
  return 0;
}

static void follow(struct printer *printer) {
  char path[512];
  snprintf(path, sizeof(path), "%s/journal.log", opt.directory);
  long offset = 0;
  FILE *f = fopen(path, "rb");
  if (f) {
    fseek(f, 0, SEEK_END);
    offset = ftell(f);
    fclose(f);
  }
  for (;;) {
    fflush(stdout);
    sleep(1);
    f = fopen(path, "rb");
    if (!f)
      continue;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < offset)
      offset = 0;
    fseek(f, offset, SEEK_SET);
    char line[LINE_CAPACITY];
    while (fgets(line, sizeof(line), f)) {
      size_t length = strlen(line);
      if (!length || line[length - 1] != '\n')
        break;
      offset += (long)length;
      struct record r;
      if (parse_record(line, &r) && matches(&r))
        print_record(&r, printer);
    }
    fclose(f);
  }
}

int main(int argc, char **argv) {
  int status = parse_arguments(argc, argv);
  if (status)
    return status == 1 ? 0 : status;
  walk(collect_boot, 0);
  if (opt.list_boots) {
    if (!boot_count) {
      fprintf(stderr, "journalctl: no journal in %s\n", opt.directory);
      return 1;
    }
    print_boots();
    return 0;
  }
  if (!boot_count) {
    fprintf(stderr, "journalctl: no journal in %s\n", opt.directory);
    return 1;
  }
  if (!resolve_boot()) {
    fprintf(stderr, "journalctl: no such boot\n");
    return 1;
  }
  struct printer printer = {0, 0, 0, false};
  long tail = opt.tail;
  if (tail < 0 && opt.follow)
    tail = 10;
  if (tail >= 0) {
    walk(count_visit, &printer);
    printer.skip = printer.counted > tail ? printer.counted - tail : 0;
  }
  walk(print_visit, &printer);
  if (opt.follow)
    follow(&printer);
  return 0;
}
