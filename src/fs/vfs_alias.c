#include "vfs_alias.h"
#include "vfs_path.h"
#include <stdbool.h>

#define ALIAS_PASSES 4

struct alias {
  const char *from;
  const char *to;
  bool exact;
};

static const struct alias aliases[] = {
    {"/bin/program/login", "/bin/login", true},
    {"/bin/program/tetris", "/usr/games/tetris", true},
    {"/bin/program/puzzle15", "/usr/games/puzzle15", true},
    {"/bin/program/2048", "/usr/games/2048", true},
    {"/bin/program/system/reboot", "/sbin/reboot", true},
    {"/bin/program/system/poweroff", "/sbin/poweroff", true},
    {"/bin/program/system/shutdown", "/sbin/shutdown", true},
    {"/bin/program/system/halt", "/sbin/halt", true},
    {"/bin/program/system/mkfs", "/sbin/mkfs", true},
    {"/bin/program/system/hddcheck", "/sbin/hddcheck", true},
    {"/bin/init", "/sbin/init", true},
    {"/bin/installer", "/sbin/installer", true},
    {"/bin/reboot", "/sbin/reboot", true},
    {"/bin/poweroff", "/sbin/poweroff", true},
    {"/bin/shutdown", "/sbin/shutdown", true},
    {"/bin/halt", "/sbin/halt", true},
    {"/bin/mkfs", "/sbin/mkfs", true},
    {"/bin/hddcheck", "/sbin/hddcheck", true},
    {"/bin/snake", "/usr/games/snake", true},
    {"/bin/tetris", "/usr/games/tetris", true},
    {"/bin/puzzle15", "/usr/games/puzzle15", true},
    {"/bin/2048", "/usr/games/2048", true},
    {"/bin/gui-demo", "/usr/bin/gui-demo", true},
    {"/bin/program/system", "/bin", false},
    {"/bin/program", "/usr/bin", false},
    {"/bin/sound", "/usr/share/sounds", false},
    {"/game/sound", "/usr/share/sounds", false},
    {"/game", "/usr/games", false},
    {"/walpaper", "/usr/share/wallpapers", false},
    {"/src/demo", "/usr/share/demo", false},
    {"/demo", "/usr/share/demo", false},
    {"/include", "/usr/include", false},
};

#define ALIAS_COUNT (sizeof(aliases) / sizeof(aliases[0]))

static uint32_t length_of(const char *text) {
  uint32_t length = 0;
  while (text[length])
    length++;
  return length;
}

static bool equal(const char *left, const char *right) {
  while (*left && *left == *right) {
    left++;
    right++;
  }
  return *left == *right;
}

static bool ends_with(const char *text, const char *suffix) {
  uint32_t text_length = length_of(text);
  uint32_t suffix_length = length_of(suffix);
  if (suffix_length > text_length)
    return false;
  return equal(text + text_length - suffix_length, suffix);
}

static int32_t rewrite_desktop(const char *path, char *out, uint32_t capacity) {
  static const char directory[] = "/bin/program/";
  static const char target[] = "/usr/share/applications/";
  uint32_t directory_length = length_of(directory);
  if (!vfs_path_has_prefix(path, "/bin/program") ||
      length_of(path) <= directory_length || !ends_with(path, ".desktop"))
    return 0;
  const char *name = path + directory_length;
  for (const char *cursor = name; *cursor; cursor++)
    if (*cursor == '/')
      return 0;
  uint32_t target_length = length_of(target);
  uint32_t name_length = length_of(name);
  if (target_length + name_length + 1 > capacity)
    return -1;
  for (uint32_t i = 0; i < target_length; i++)
    out[i] = target[i];
  for (uint32_t i = 0; i <= name_length; i++)
    out[target_length + i] = name[i];
  return 1;
}

static int32_t rewrite_once(const char *path, char *out, uint32_t capacity) {
  int32_t desktop = rewrite_desktop(path, out, capacity);
  if (desktop)
    return desktop;
  for (uint32_t i = 0; i < ALIAS_COUNT; i++) {
    const struct alias *alias = &aliases[i];
    if (alias->exact ? !equal(path, alias->from)
                     : !vfs_path_has_prefix(path, alias->from))
      continue;
    const char *rest = path + length_of(alias->from);
    uint32_t to_length = length_of(alias->to);
    uint32_t rest_length = length_of(rest);
    if (to_length + rest_length + 1 > capacity)
      return -1;
    for (uint32_t k = 0; k < to_length; k++)
      out[k] = alias->to[k];
    for (uint32_t k = 0; k <= rest_length; k++)
      out[to_length + k] = rest[k];
    return 1;
  }
  return 0;
}

int32_t vfs_alias_resolve(const char *path, char *out, uint32_t capacity) {
  char current[VFS_PATH_MAX];
  char next[VFS_PATH_MAX];
  if (!path || !out || capacity < 2)
    return -3;
  int32_t length = vfs_path_normalize(path, current, sizeof(current));
  if (length < 0)
    return length;
  for (int pass = 0; pass < ALIAS_PASSES; pass++) {
    int32_t changed = rewrite_once(current, next, sizeof(next));
    if (changed < 0)
      return -3;
    if (!changed)
      break;
    length = vfs_path_normalize(next, current, sizeof(current));
    if (length < 0)
      return length;
  }
  if ((uint32_t)length + 1 > capacity)
    return -3;
  for (int32_t i = 0; i <= length; i++)
    out[i] = current[i];
  return length;
}
