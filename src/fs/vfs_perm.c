#include "vfs_perm.h"
#include "types/fs_types.h"

#define PERM_TABLE_MAX 512
#define PERM_PATH_MAX 256

struct perm_slot {
  bool used;
  char path[PERM_PATH_MAX];
  struct vfs_perm_meta meta;
};

static struct perm_slot table[PERM_TABLE_MAX];

static uint32_t text_length(const char *text) {
  uint32_t length = 0;
  while (text[length])
    length++;
  return length;
}

static bool text_equal(const char *left, const char *right) {
  while (*left && *left == *right) {
    left++;
    right++;
  }
  return *left == *right;
}

static bool path_has_prefix(const char *path, const char *prefix) {
  uint32_t length = text_length(prefix);
  for (uint32_t i = 0; i < length; i++)
    if (path[i] != prefix[i])
      return false;
  return path[length] == '\0' || path[length] == '/';
}

static bool copy_text(char *destination, uint32_t capacity,
                      const char *source) {
  uint32_t length = text_length(source);
  if (length >= capacity)
    return false;
  for (uint32_t i = 0; i <= length; i++)
    destination[i] = source[i];
  return true;
}

static struct perm_slot *find_slot(const char *path) {
  for (uint32_t i = 0; i < PERM_TABLE_MAX; i++)
    if (table[i].used && text_equal(table[i].path, path))
      return &table[i];
  return 0;
}

bool vfs_perm_allowed(uint32_t uid, uint32_t gid,
                      const struct vfs_perm_meta *meta, uint32_t want) {
  if (uid == 0)
    return true;
  uint32_t bits;
  if (uid == meta->uid)
    bits = (meta->mode >> 6) & 7u;
  else if (gid == meta->gid)
    bits = (meta->mode >> 3) & 7u;
  else
    bits = meta->mode & 7u;
  return (bits & want) == (want & 7u);
}

void vfs_perm_default(struct vfs_perm_meta *meta, bool directory) {
  meta->mode = directory ? VFS_MODE_DEFAULT_DIR : VFS_MODE_DEFAULT_FILE;
  meta->uid = 0;
  meta->gid = 0;
}

int32_t vfs_perm_set(const char *path, const struct vfs_perm_meta *meta) {
  if (!path || !meta || text_length(path) >= PERM_PATH_MAX)
    return FS_ERROR_INVALID;
  struct perm_slot *slot = find_slot(path);
  if (!slot) {
    for (uint32_t i = 0; i < PERM_TABLE_MAX; i++)
      if (!table[i].used) {
        slot = &table[i];
        break;
      }
    if (!slot)
      return FS_ERROR_NO_SPACE;
    copy_text(slot->path, PERM_PATH_MAX, path);
    slot->used = true;
  }
  slot->meta = *meta;
  slot->meta.mode &= VFS_MODE_MASK;
  return 0;
}

bool vfs_perm_get(const char *path, struct vfs_perm_meta *meta) {
  struct perm_slot *slot = find_slot(path);
  if (!slot)
    return false;
  *meta = slot->meta;
  return true;
}

int32_t vfs_perm_chmod(const char *path, uint16_t mode) {
  struct vfs_perm_meta meta;
  if (!vfs_perm_get(path, &meta))
    vfs_perm_default(&meta, false);
  meta.mode = mode & VFS_MODE_MASK;
  return vfs_perm_set(path, &meta);
}

int32_t vfs_perm_chown(const char *path, uint32_t uid, uint32_t gid) {
  struct vfs_perm_meta meta;
  if (!vfs_perm_get(path, &meta))
    vfs_perm_default(&meta, false);
  if (uid != USER_ID_NONE)
    meta.uid = uid;
  if (gid != USER_ID_NONE)
    meta.gid = gid;
  return vfs_perm_set(path, &meta);
}

void vfs_perm_forget(const char *path) {
  for (uint32_t i = 0; i < PERM_TABLE_MAX; i++)
    if (table[i].used && path_has_prefix(table[i].path, path))
      table[i].used = false;
}

void vfs_perm_rename(const char *from, const char *to) {
  uint32_t from_length = text_length(from);
  for (uint32_t i = 0; i < PERM_TABLE_MAX; i++) {
    if (!table[i].used || !path_has_prefix(table[i].path, from))
      continue;
    char moved[PERM_PATH_MAX];
    if (!copy_text(moved, PERM_PATH_MAX, to))
      continue;
    uint32_t to_length = text_length(moved);
    const char *tail = table[i].path + from_length;
    if (to_length + text_length(tail) >= PERM_PATH_MAX) {
      table[i].used = false;
      continue;
    }
    copy_text(moved + to_length, PERM_PATH_MAX - to_length, tail);
    copy_text(table[i].path, PERM_PATH_MAX, moved);
  }
}

void vfs_perm_reset(void) {
  for (uint32_t i = 0; i < PERM_TABLE_MAX; i++)
    table[i].used = false;
}

static bool next_field(const char **cursor, char *out, uint32_t capacity) {
  const char *p = *cursor;
  uint32_t length = 0;
  while (*p && *p != ':' && *p != '\n' && *p != '\r') {
    if (length + 1 >= capacity)
      return false;
    out[length++] = *p++;
  }
  out[length] = '\0';
  *cursor = p;
  return true;
}

static bool skip_colon(const char **cursor) {
  if (**cursor != ':')
    return false;
  (*cursor)++;
  return true;
}

static bool parse_number(const char *text, uint32_t *value) {
  if (!*text)
    return false;
  uint64_t result = 0;
  for (; *text; text++) {
    if (*text < '0' || *text > '9')
      return false;
    result = result * 10 + (uint64_t)(*text - '0');
    if (result >= USER_ID_NONE)
      return false;
  }
  *value = (uint32_t)result;
  return true;
}

bool user_parse_line(const char *line, struct user_entry *out) {
  if (!line || !out || !*line || *line == '#')
    return false;
  const char *p = line;
  char password[8];
  char number[16];
  if (!next_field(&p, out->name, sizeof(out->name)) || !out->name[0])
    return false;
  if (!skip_colon(&p) || !next_field(&p, password, sizeof(password)))
    return false;
  if (!skip_colon(&p) || !next_field(&p, number, sizeof(number)) ||
      !parse_number(number, &out->uid))
    return false;
  if (!skip_colon(&p) || !next_field(&p, number, sizeof(number)) ||
      !parse_number(number, &out->gid))
    return false;
  if (!skip_colon(&p) || !next_field(&p, out->gecos, sizeof(out->gecos)))
    return false;
  if (!skip_colon(&p) || !next_field(&p, out->home, sizeof(out->home)))
    return false;
  if (!skip_colon(&p) || !next_field(&p, out->shell, sizeof(out->shell)))
    return false;
  return true;
}

static bool scan_users(const char *text, const char *name, uint32_t uid,
                       bool by_name, struct user_entry *out) {
  if (!text || !out)
    return false;
  const char *p = text;
  while (*p) {
    struct user_entry entry;
    if (user_parse_line(p, &entry) &&
        (by_name ? text_equal(entry.name, name) : entry.uid == uid)) {
      *out = entry;
      return true;
    }
    while (*p && *p != '\n')
      p++;
    if (*p == '\n')
      p++;
  }
  return false;
}

bool user_lookup_name(const char *text, const char *name,
                      struct user_entry *out) {
  return name && scan_users(text, name, 0, true, out);
}

bool user_lookup_uid(const char *text, uint32_t uid, struct user_entry *out) {
  return scan_users(text, 0, uid, false, out);
}

static bool put_text(char *out, uint32_t capacity, uint32_t *position,
                     const char *text) {
  uint32_t length = text_length(text);
  if (*position + length >= capacity)
    return false;
  for (uint32_t i = 0; i < length; i++)
    out[(*position)++] = text[i];
  out[*position] = '\0';
  return true;
}

static bool put_number(char *out, uint32_t capacity, uint32_t *position,
                       uint32_t value) {
  char digits[12];
  uint32_t count = 0;
  do {
    digits[count++] = (char)('0' + value % 10u);
    value /= 10u;
  } while (value);
  char text[12];
  for (uint32_t i = 0; i < count; i++)
    text[i] = digits[count - 1 - i];
  text[count] = '\0';
  return put_text(out, capacity, position, text);
}

int32_t user_format_line(const struct user_entry *entry, char *out,
                         uint32_t capacity) {
  if (!entry || !out || !capacity)
    return FS_ERROR_INVALID;
  uint32_t position = 0;
  out[0] = '\0';
  if (!put_text(out, capacity, &position, entry->name) ||
      !put_text(out, capacity, &position, ":x:") ||
      !put_number(out, capacity, &position, entry->uid) ||
      !put_text(out, capacity, &position, ":") ||
      !put_number(out, capacity, &position, entry->gid) ||
      !put_text(out, capacity, &position, ":") ||
      !put_text(out, capacity, &position, entry->gecos) ||
      !put_text(out, capacity, &position, ":") ||
      !put_text(out, capacity, &position, entry->home) ||
      !put_text(out, capacity, &position, ":") ||
      !put_text(out, capacity, &position, entry->shell) ||
      !put_text(out, capacity, &position, "\n"))
    return FS_ERROR_NO_SPACE;
  return (int32_t)position;
}
