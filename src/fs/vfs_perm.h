#pragma once

#include <stdbool.h>
#include <stdint.h>

#define VFS_PERM_X 1u
#define VFS_PERM_W 2u
#define VFS_PERM_R 4u

#define VFS_MODE_DEFAULT_FILE 0644u
#define VFS_MODE_DEFAULT_DIR 0755u
#define VFS_MODE_MASK 07777u

#define USER_NAME_MAX 32
#define USER_GECOS_MAX 64
#define USER_HOME_MAX 64
#define USER_SHELL_MAX 64
#define USER_ID_NONE 0xFFFFFFFFu

struct vfs_perm_meta {
  uint16_t mode;
  uint32_t uid;
  uint32_t gid;
};

struct user_entry {
  char name[USER_NAME_MAX];
  uint32_t uid;
  uint32_t gid;
  char gecos[USER_GECOS_MAX];
  char home[USER_HOME_MAX];
  char shell[USER_SHELL_MAX];
};

bool vfs_perm_allowed(uint32_t uid, uint32_t gid,
                      const struct vfs_perm_meta *meta, uint32_t want);
void vfs_perm_default(struct vfs_perm_meta *meta, bool directory);
int32_t vfs_perm_set(const char *path, const struct vfs_perm_meta *meta);
bool vfs_perm_get(const char *path, struct vfs_perm_meta *meta);
int32_t vfs_perm_chmod(const char *path, uint16_t mode);
int32_t vfs_perm_chown(const char *path, uint32_t uid, uint32_t gid);
void vfs_perm_forget(const char *path);
void vfs_perm_rename(const char *from, const char *to);
void vfs_perm_reset(void);

bool user_parse_line(const char *line, struct user_entry *out);
bool user_lookup_name(const char *text, const char *name,
                      struct user_entry *out);
bool user_lookup_uid(const char *text, uint32_t uid, struct user_entry *out);
int32_t user_format_line(const struct user_entry *entry, char *out,
                         uint32_t capacity);
