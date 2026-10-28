#include "vfs_path.h"

#define PATH_INVALID -3
#define PATH_TOO_LONG -3

static uint32_t length_of(const char *text) {
  uint32_t length = 0;
  while (text[length])
    length++;
  return length;
}

int32_t vfs_path_normalize(const char *path, char *out, uint32_t capacity) {
  if (!path || !out || capacity < 2 || path[0] != '/')
    return PATH_INVALID;
  uint32_t length = 1;
  out[0] = '/';
  uint32_t index = 0;
  while (path[index]) {
    while (path[index] == '/')
      index++;
    uint32_t start = index;
    while (path[index] && path[index] != '/')
      index++;
    uint32_t size = index - start;
    if (!size)
      break;
    if (size == 1 && path[start] == '.')
      continue;
    if (size == 2 && path[start] == '.' && path[start + 1] == '.') {
      if (length > 1) {
        length--;
        while (length > 1 && out[length - 1] != '/')
          length--;
        if (length > 1)
          length--;
      }
      continue;
    }
    if (size > VFS_NAME_MAX)
      return PATH_TOO_LONG;
    uint32_t separator = length > 1 ? 1 : 0;
    if (length + separator + size + 1 > capacity)
      return PATH_TOO_LONG;
    if (separator)
      out[length++] = '/';
    for (uint32_t i = 0; i < size; i++)
      out[length++] = path[start + i];
  }
  out[length] = '\0';
  return (int32_t)length;
}

bool vfs_path_has_prefix(const char *path, const char *prefix) {
  uint32_t prefix_length = length_of(prefix);
  if (prefix_length == 1 && prefix[0] == '/')
    return path[0] == '/';
  for (uint32_t i = 0; i < prefix_length; i++)
    if (path[i] != prefix[i])
      return false;
  return path[prefix_length] == '\0' || path[prefix_length] == '/';
}

int32_t vfs_path_parent(const char *path, char *out, uint32_t capacity) {
  uint32_t length = length_of(path);
  if (!length || path[0] != '/' || capacity < 2)
    return PATH_INVALID;
  while (length > 1 && path[length - 1] == '/')
    length--;
  while (length > 1 && path[length - 1] != '/')
    length--;
  if (length > 1)
    length--;
  if (length + 1 > capacity)
    return PATH_TOO_LONG;
  for (uint32_t i = 0; i < length; i++)
    out[i] = path[i];
  out[length] = '\0';
  return (int32_t)length;
}

const char *vfs_path_basename(const char *path) {
  const char *name = path;
  for (const char *cursor = path; *cursor; cursor++)
    if (*cursor == '/' && cursor[1])
      name = cursor + 1;
  return name;
}
