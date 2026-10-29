#include <assert.h>
#include <string.h>

int pc_sscanf(const char *input, const char *format, ...);

static void test_integers(void) {
  int v = 0;
  unsigned u = 0;
  assert(pc_sscanf(" 0x1F", " 0x%x", &u) == 1 && u == 0x1F);
  assert(pc_sscanf(" 0X1f", " 0X%x", &u) == 1 && u == 0x1F);
  assert(pc_sscanf(" 017", " 0%o", &u) == 1 && u == 15);
  assert(pc_sscanf(" -42", " %d", &v) == 1 && v == -42);
  assert(pc_sscanf("+7", "%d", &v) == 1 && v == 7);
  assert(pc_sscanf("0x20", "%i", &v) == 1 && v == 32);
  assert(pc_sscanf("010", "%i", &v) == 1 && v == 8);
  assert(pc_sscanf("99", "%i", &v) == 1 && v == 99);
  assert(pc_sscanf("ff", "%x", &u) == 1 && u == 255);
  assert(pc_sscanf("12345", "%3d", &v) == 1 && v == 123);
}

static void test_failures(void) {
  int v = 5;
  assert(pc_sscanf("abc", "%d", &v) == 0 && v == 5);
  assert(pc_sscanf("", "%d", &v) == -1);
  assert(pc_sscanf("   ", "%d", &v) == -1);
  assert(pc_sscanf("-", "%d", &v) == 0);
  assert(pc_sscanf("12", "x%d", &v) == 0);
}

static void test_strings(void) {
  char a[16], b[16];
  assert(pc_sscanf("hello world", "%s %s", a, b) == 2);
  assert(!strcmp(a, "hello") && !strcmp(b, "world"));
  assert(pc_sscanf("abcdef", "%3s", a) == 1 && !strcmp(a, "abc"));
  assert(pc_sscanf("key=value", "%[^=]=%s", a, b) == 2);
  assert(!strcmp(a, "key") && !strcmp(b, "value"));
  assert(pc_sscanf("aabbcc", "%[ab]", a) == 1 && !strcmp(a, "aabb"));
  assert(pc_sscanf("xyz", "%c", a) == 1 && a[0] == 'x');
}

static void test_misc(void) {
  int v = 0, w = 0, n = 0;
  long long ll = 0;
  short s = 0;
  double d = 0;
  float f = 0;
  assert(pc_sscanf("10 20", "%d %*d", &v) == 1 && v == 10);
  assert(pc_sscanf("10 20", "%d%n", &v, &n) == 1 && n == 2);
  assert(pc_sscanf("5%", "%d%%", &v) == 1 && v == 5);
  assert(pc_sscanf("1 2", "%d %d", &v, &w) == 2 && v == 1 && w == 2);
  assert(pc_sscanf("-9000000000", "%lld", &ll) == 1 && ll == -9000000000LL);
  assert(pc_sscanf("-5", "%hd", &s) == 1 && s == -5);
  assert(pc_sscanf("3.25", "%lf", &d) == 1 && d == 3.25);
  assert(pc_sscanf("-1.5e2", "%f", &f) == 1 && f == -150.0f);
}

int main(void) {
  test_integers();
  test_failures();
  test_strings();
  test_misc();
  return 0;
}
