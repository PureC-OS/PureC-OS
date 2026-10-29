#include "fs/vfs_perm.h"

#include <assert.h>
#include <string.h>

static void test_allowed(void) {
  struct vfs_perm_meta meta = {0640, 1000, 100};
  assert(vfs_perm_allowed(0, 0, &meta, VFS_PERM_W));
  assert(vfs_perm_allowed(1000, 5, &meta, VFS_PERM_R | VFS_PERM_W));
  assert(!vfs_perm_allowed(1000, 5, &meta, VFS_PERM_X));
  assert(vfs_perm_allowed(2000, 100, &meta, VFS_PERM_R));
  assert(!vfs_perm_allowed(2000, 100, &meta, VFS_PERM_W));
  assert(!vfs_perm_allowed(2000, 200, &meta, VFS_PERM_R));
}

static void test_table(void) {
  vfs_perm_reset();
  struct vfs_perm_meta meta;
  assert(!vfs_perm_get("/home/a", &meta));
  assert(vfs_perm_chown("/home/a", 1000, 1000) == 0);
  assert(vfs_perm_chmod("/home/a", 0700) == 0);
  assert(vfs_perm_get("/home/a", &meta));
  assert(meta.uid == 1000 && meta.gid == 1000 && meta.mode == 0700);
  assert(vfs_perm_chown("/home/a", USER_ID_NONE, 50) == 0);
  vfs_perm_get("/home/a", &meta);
  assert(meta.uid == 1000 && meta.gid == 50);
  vfs_perm_chmod("/home/a/f", 0600);
  vfs_perm_rename("/home/a", "/home/b");
  assert(!vfs_perm_get("/home/a", &meta));
  assert(vfs_perm_get("/home/b", &meta));
  assert(vfs_perm_get("/home/b/f", &meta) && meta.mode == 0600);
  vfs_perm_chmod("/home/bob", 0755);
  vfs_perm_forget("/home/b");
  assert(!vfs_perm_get("/home/b", &meta));
  assert(!vfs_perm_get("/home/b/f", &meta));
  assert(vfs_perm_get("/home/bob", &meta));
}

static void test_users(void) {
  const char *db = "# c\nroot:x:0:0:root:/root:/usr/bin/terminal\n"
                   "bad line\nann:x:1000:1000:Ann:/home/ann:/bin/sh\n";
  struct user_entry e;
  assert(user_lookup_name("root:x:0:0:root:/root:/bin/sh\n", "root", &e));
  assert(e.uid == 0 && strcmp(e.home, "/root") == 0);
  assert(!user_lookup_name(db, "bob", &e));
  assert(user_lookup_uid("a:x:7:7:A:/h:/s\nb:x:8:8:B:/h2:/s2\n", 8, &e));
  assert(strcmp(e.name, "b") == 0 && strcmp(e.home, "/h2") == 0);
  assert(user_lookup_uid("a:x:7:7:A:/h:/s", 7, &e));
  assert(!user_parse_line("a:x:notnum:0:A:/h:/s", &e));
  assert(user_parse_line("u:x:1001:100:U:/home/u:/bin/sh", &e));
  char line[128];
  assert(user_format_line(&e, line, sizeof(line)) > 0);
  assert(strcmp(line, "u:x:1001:100:U:/home/u:/bin/sh\n") == 0);
  assert(user_format_line(&e, line, 8) < 0);
}

int main(void) {
  test_allowed();
  test_table();
  test_users();
  return 0;
}
