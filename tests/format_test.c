#include "../src/lib/format.h"
#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *expected, const char *actual, int line) {
  if (strcmp(expected, actual) != 0) {
    printf("FAIL line %d: expected [%s] got [%s]\n", line, expected, actual);
    failures++;
  }
}

#define EXPECT(expected, ...)                                                  \
  do {                                                                         \
    char buffer[128];                                                          \
    kformat(buffer, sizeof(buffer), __VA_ARGS__);                              \
    check(expected, buffer, __LINE__);                                         \
  } while (0)

int main(void) {
  EXPECT("hello", "hello");
  EXPECT("a=5 b=-7", "a=%u b=%d", 5u, -7);
  EXPECT("ff 00ab", "%x %04x", 255u, 0xabu);
  EXPECT("18446744073709551615", "%llu", 18446744073709551615ULL);
  EXPECT("-9223372036854775807", "%lld", -9223372036854775807LL);
  EXPECT("  42|42  |", "%4u|%-4u|", 42u, 42u);
  EXPECT("-0042", "%05d", -42);
  EXPECT("[abc] [  abc]", "[%s] [%5s]", "abc", "abc");
  EXPECT("100%", "100%%");
  EXPECT("x", "%c", 'x');
  const char *volatile none = 0;
  EXPECT("(null)", "%s", none);
  EXPECT("0", "%u", 0u);
  char small[8];
  int32_t length = kformat(small, sizeof(small), "%s", "0123456789");
  check("0123456", small, __LINE__);
  if (length != 10) {
    printf("FAIL: truncated length %d\n", length);
    failures++;
  }
  if (failures) {
    printf("format_test: %d failure(s)\n", failures);
    return 1;
  }
  printf("format_test: ok\n");
  return 0;
}
