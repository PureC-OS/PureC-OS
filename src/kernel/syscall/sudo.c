#include "sudo.h"

#include "../../fs/vfs.h"
#include "../../lib/string.h"
#include "../diagnostics/klog.h"
#include "../process/process.h"
#include "purecrypt.h"
#include "sha512.h"

#define SUDO_FILE_MAX 8192
#define SUDO_GROUP "wheel"

static char sudo_buffer[SUDO_FILE_MAX];

static uint32_t read_whole(const char *path)
{
  int32_t fd = vfs_open_trusted(path);
  if (fd < 0)
    return 0;
  uint32_t total = 0;
  while (total + 1 < sizeof(sudo_buffer))
  {
    int32_t got = vfs_read(fd, sudo_buffer + total,
                           (uint32_t)(sizeof(sudo_buffer) - 1 - total));
    if (got <= 0)
      break;
    total += (uint32_t)got;
  }
  vfs_close(fd);
  sudo_buffer[total] = '\0';
  return total;
}

static const char *next_line(const char *line)
{
  while (*line && *line != '\n')
    line++;
  return *line ? line + 1 : line;
}

static const char *field(const char *line, uint32_t index)
{
  for (uint32_t i = 0; i < index; i++)
  {
    while (*line && *line != ':' && *line != '\n')
      line++;
    if (*line != ':')
      return 0;
    line++;
  }
  return line;
}

static bool name_matches(const char *text, const char *name, char end)
{
  uint32_t n = (uint32_t)strlen(name);
  return strncmp(text, name, n) == 0 && (text[n] == end || text[n] == '\n' ||
                                          text[n] == '\0');
}

static bool in_wheel(const char *name, uint32_t gid)
{
  if (!read_whole("/etc/group"))
    return false;
  for (const char *line = sudo_buffer; *line; line = next_line(line))
  {
    if (!name_matches(line, SUDO_GROUP, ':'))
      continue;
    const char *gid_text = field(line, 2);
    if (gid_text)
    {
      uint32_t value = 0;
      bool digits = false;
      while (*gid_text >= '0' && *gid_text <= '9')
      {
        value = value * 10 + (uint32_t)(*gid_text++ - '0');
        digits = true;
      }
      if (digits && value == gid)
        return true;
    }
    const char *member = field(line, 3);
    while (member && *member && *member != '\n')
    {
      if (name_matches(member, name, ','))
        return true;
      while (*member && *member != ',' && *member != '\n')
        member++;
      if (*member == ',')
        member++;
    }
    return false;
  }
  return false;
}

static bool password_ok(const char *name, const char *password)
{
  if (!read_whole("/etc/shadow"))
    return false;
  bool ok = false;
  for (const char *line = sudo_buffer; *line; line = next_line(line))
  {
    if (!name_matches(line, name, ':'))
      continue;
    const char *hash = field(line, 1);
    if (!hash)
      break;
    char expected[PURECRYPT_HASH_MAX];
    uint32_t len = 0;
    while (hash[len] && hash[len] != ':' && hash[len] != '\n' &&
           len + 1 < sizeof(expected))
    {
      expected[len] = hash[len];
      len++;
    }
    expected[len] = '\0';
    ok = len > 0 && purecrypt_verify(password, expected) == 1;
    pure_memzero(expected, sizeof(expected));
    break;
  }
  pure_memzero(sudo_buffer, sizeof(sudo_buffer));
  return ok;
}

int32_t sudo_elevate(const char *password)
{
  uint32_t uid = process_current_uid();
  if (uid == 0)
    return 0;
  struct user_info info;
  if (vfs_user_lookup(0, uid, &info) < 0)
    return -1;
  bool allowed = in_wheel(info.name, info.gid);
  bool ok = allowed && password_ok(info.name, password);
  if (!ok)
  {
    klogf(KLOG_WARN, "sudo: denied for %s (uid=%u pid=%d)%s", info.name, uid,
          process_current_pid(), allowed ? "" : ", not in wheel");
    return allowed ? -1 : -2;
  }
  process_grant_root();
  klogf(KLOG_INFO, "sudo: %s (uid=%u) became root, pid=%d", info.name, uid,
        process_current_pid());
  return 0;
}
