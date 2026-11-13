#include "user_config.h"

#include "../fs/vfs.h"
#include "../kernel/process/process.h"
#include "../kernel/syscall/syscall.h"
#include "../lib/string.h"

static bool join(char *out, uint32_t capacity, const char *a, const char *b,
                 const char *c)
{
  uint32_t n = 0;
  const char *parts[3] = {a, b, c};
  for (uint32_t p = 0; p < 3; p++)
    for (const char *s = parts[p]; s && *s; s++)
    {
      if (n + 1 >= capacity)
        return false;
      out[n++] = *s;
    }
  out[n] = '\0';
  return true;
}

static bool session_home(char *home, uint32_t capacity, uint32_t *uid,
                         uint32_t *gid)
{
  if (!process_session_user(uid, gid))
    return false;
  struct user_info info;
  if (vfs_user_lookup(0, *uid, &info) < 0 || !info.home[0])
    return false;
  return join(home, capacity, info.home, 0, 0);
}

static bool exists(const char *path)
{
  int32_t fd = vfs_open_trusted(path);
  if (fd < 0)
    return false;
  (void)vfs_close(fd);
  return true;
}

bool user_config_path(const char *name, char *out, uint32_t capacity,
                      bool for_write)
{
  char home[64];
  uint32_t uid = 0, gid = 0;
  if (session_home(home, sizeof(home), &uid, &gid))
  {
    char dir[128];
    if (join(dir, sizeof(dir), home, USER_CONFIG_SUBDIR, 0) &&
        join(out, capacity, dir, "/", name))
    {
      if (for_write)
      {
        char parent[96];
        if (join(parent, sizeof(parent), home, "/.config", 0) &&
            vfs_create_directory(parent) >= 0)
          (void)vfs_chown(parent, uid, gid);
        if (vfs_create_directory(dir) >= 0)
          (void)vfs_chown(dir, uid, gid);
        return true;
      }
      if (exists(out))
        return true;
    }
  }
  if (for_write)
    (void)vfs_create_directory(USER_CONFIG_SYSTEM_DIR);
  return join(out, capacity, USER_CONFIG_SYSTEM_DIR, "/", name);
}

void user_config_claim(const char *path)
{
  uint32_t uid = 0, gid = 0;
  if (!path || !process_session_user(&uid, &gid))
    return;
  if (strncmp(path, USER_CONFIG_SYSTEM_DIR "/",
              sizeof(USER_CONFIG_SYSTEM_DIR)) == 0)
    return;
  (void)vfs_chown(path, uid, gid);
}
