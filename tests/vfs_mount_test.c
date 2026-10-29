#include "../src/fs/vfs_mount.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition);               \
      failures++;                                                              \
    }                                                                          \
  } while (0)

static void expect_normalized(const char *input, const char *expected) {
  char out[VFS_PATH_MAX];
  int32_t result = vfs_path_normalize(input, out, sizeof(out));
  if (!expected) {
    CHECK(result < 0);
    return;
  }
  CHECK(result == (int32_t)strlen(expected));
  CHECK(strcmp(out, expected) == 0);
  if (strcmp(out, expected) != 0)
    printf("  %s -> %s (expected %s)\n", input, out, expected);
}

static void test_normalize(void) {
  expect_normalized("/", "/");
  expect_normalized("//", "/");
  expect_normalized("/usr/bin", "/usr/bin");
  expect_normalized("/usr/bin/", "/usr/bin");
  expect_normalized("/usr//bin///ls", "/usr/bin/ls");
  expect_normalized("/usr/./bin/.", "/usr/bin");
  expect_normalized("/usr/bin/..", "/usr");
  expect_normalized("/usr/bin/../lib", "/usr/lib");
  expect_normalized("/..", "/");
  expect_normalized("/../../etc", "/etc");
  expect_normalized("/a/b/../../..", "/");
  expect_normalized("relative/path", NULL);
  expect_normalized("", NULL);
}

static void test_parent_and_basename(void) {
  char out[VFS_PATH_MAX];
  CHECK(vfs_path_parent("/usr/bin/ls", out, sizeof(out)) == 8);
  CHECK(strcmp(out, "/usr/bin") == 0);
  CHECK(vfs_path_parent("/usr", out, sizeof(out)) == 1);
  CHECK(strcmp(out, "/") == 0);
  CHECK(vfs_path_parent("/", out, sizeof(out)) == 1);
  CHECK(strcmp(out, "/") == 0);
  CHECK(strcmp(vfs_path_basename("/usr/bin/ls"), "ls") == 0);
  CHECK(strcmp(vfs_path_basename("/usr"), "usr") == 0);
  CHECK(strcmp(vfs_path_basename("/"), "/") == 0);
}

static void test_prefix(void) {
  CHECK(vfs_path_has_prefix("/dev", "/dev"));
  CHECK(vfs_path_has_prefix("/dev/sda", "/dev"));
  CHECK(!vfs_path_has_prefix("/device", "/dev"));
  CHECK(vfs_path_has_prefix("/anything", "/"));
  CHECK(!vfs_path_has_prefix("/de", "/dev"));
}

static const struct vfs_fs_ops root_ops = {.name = "ext2"};
static const struct vfs_fs_ops dev_ops = {.name = "devfs"};
static const struct vfs_fs_ops proc_ops = {.name = "procfs"};

static void test_mounts(void) {
  char buffer[VFS_PATH_MAX];
  const char *relative = 0;
  vfs_mount_reset();
  CHECK(vfs_mount_find("/etc", &relative, buffer, sizeof(buffer)) == 0);
  CHECK(vfs_mount_add("/", &root_ops, 0, "sda1", 0) == 0);
  CHECK(vfs_mount_add("/dev", &dev_ops, 0, "devfs", 0) == 0);
  CHECK(vfs_mount_add("/proc", &proc_ops, 0, "proc", VFS_MOUNT_READ_ONLY) == 0);
  CHECK(vfs_mount_add("/dev", &dev_ops, 0, "devfs", 0) == FS_ERROR_BUSY);
  CHECK(vfs_mount_add("relative", &dev_ops, 0, 0, 0) == FS_ERROR_INVALID);
  CHECK(vfs_mount_count() == 3);

  struct vfs_mount *mount = vfs_mount_find("/etc/passwd", &relative, buffer, sizeof(buffer));
  CHECK(mount && mount->ops == &root_ops);
  CHECK(relative && strcmp(relative, "/etc/passwd") == 0);

  mount = vfs_mount_find("/dev/sda1", &relative, buffer, sizeof(buffer));
  CHECK(mount && mount->ops == &dev_ops);
  CHECK(relative && strcmp(relative, "/sda1") == 0);

  mount = vfs_mount_find("/dev", &relative, buffer, sizeof(buffer));
  CHECK(mount && mount->ops == &dev_ops);
  CHECK(relative && strcmp(relative, "/") == 0);

  mount = vfs_mount_find("/device", &relative, buffer, sizeof(buffer));
  CHECK(mount && mount->ops == &root_ops);
  CHECK(relative && strcmp(relative, "/device") == 0);

  const char *names[8];
  CHECK(vfs_mount_children("/", names, 8) == 2);
  CHECK(vfs_mount_children("/dev", names, 8) == 0);

  CHECK(vfs_mount_add("/dev/pts", &dev_ops, 0, "devpts", 0) == 0);
  CHECK(vfs_mount_children("/dev", names, 8) == 1);
  CHECK(strcmp(names[0], "pts") == 0);
  CHECK(vfs_mount_remove("/dev") == FS_ERROR_BUSY);
  CHECK(vfs_mount_remove("/dev/pts") == 0);
  CHECK(vfs_mount_remove("/dev") == 0);
  CHECK(vfs_mount_remove("/dev") == FS_ERROR_NOT_FOUND);
  CHECK(vfs_mount_count() == 2);
}

int main(void) {
  test_normalize();
  test_parent_and_basename();
  test_prefix();
  test_mounts();
  if (failures) {
    printf("vfs_mount_test: %d failure(s)\n", failures);
    return 1;
  }
  printf("vfs_mount_test: ok\n");
  return 0;
}
