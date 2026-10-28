#include "../src/fs/vfs_alias.h"
#include <stdio.h>
#include <string.h>

static int failures;

static void expect(const char *input, const char *expected) {
  char out[512];
  int32_t length = vfs_alias_resolve(input, out, sizeof(out));
  if (!expected) {
    if (length >= 0) {
      printf("FAIL %s: expected error, got %s\n", input, out);
      failures++;
    }
    return;
  }
  if (length < 0 || strcmp(out, expected) != 0) {
    printf("FAIL %s: expected %s got %s\n", input, expected, length < 0 ? "error" : out);
    failures++;
  }
}

int main(void) {
  expect("/bin/program/terminal", "/usr/bin/terminal");
  expect("/bin/program/", "/usr/bin");
  expect("/bin/program", "/usr/bin");
  expect("/bin/program/login", "/bin/login");
  expect("/bin/program/system/ls", "/bin/ls");
  expect("/bin/program/system/reboot", "/sbin/reboot");
  expect("/bin/program/system", "/bin");
  expect("/bin/program/terminal.desktop", "/usr/share/applications/terminal.desktop");
  expect("/bin/program/tetris.desktop", "/usr/share/applications/tetris.desktop");
  expect("/bin/program/tetris", "/usr/games/tetris");
  expect("/bin/tetris", "/usr/games/tetris");
  expect("/game/snake", "/usr/games/snake");
  expect("/game/sound/eat.wav", "/usr/share/sounds/eat.wav");
  expect("/bin/sound/die.wav", "/usr/share/sounds/die.wav");
  expect("/bin/init", "/sbin/init");
  expect("/bin/installer", "/sbin/installer");
  expect("/bin/reboot", "/sbin/reboot");
  expect("/bin/ls", "/bin/ls");
  expect("/bin/cat", "/bin/cat");
  expect("/bin/system", "/bin/system");
  expect("/bin/modules/e1000.elf", "/bin/modules/e1000.elf");
  expect("/bin/firmware/Intel/wifi/x.ucode", "/bin/firmware/Intel/wifi/x.ucode");
  expect("/walpaper/wp01.png", "/usr/share/wallpapers/wp01.png");
  expect("/demo/wplist.ini", "/usr/share/demo/wplist.ini");
  expect("/src/demo/image.png", "/usr/share/demo/image.png");
  expect("/include/stdio.h", "/usr/include/stdio.h");
  expect("/include/sys/stat.h", "/usr/include/sys/stat.h");
  expect("/includes/x", "/includes/x");
  expect("/gamer", "/gamer");
  expect("/usr/bin/terminal", "/usr/bin/terminal");
  expect("/usr/games/snake", "/usr/games/snake");
  expect("/sbin/init", "/sbin/init");
  expect("/lib/crt0.o", "/lib/crt0.o");
  expect("/", "/");
  expect("/bin/program/../program/nano", "/usr/bin/nano");
  expect("/bin/program//nano", "/usr/bin/nano");
  expect("relative", NULL);
  char tiny[8];
  if (vfs_alias_resolve("/bin/program/terminal", tiny, sizeof(tiny)) >= 0) {
    printf("FAIL: small buffer accepted\n");
    failures++;
  }
  if (failures) {
    printf("vfs_alias_test: %d failure(s)\n", failures);
    return 1;
  }
  printf("vfs_alias_test: ok\n");
  return 0;
}
