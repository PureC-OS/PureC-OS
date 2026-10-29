#include "format.h"
#include <stdbool.h>

struct sink {
  char *out;
  uint32_t capacity;
  uint32_t length;
};

static void put(struct sink *sink, char c) {
  if (sink->length + 1 < sink->capacity)
    sink->out[sink->length] = c;
  sink->length++;
}

static void put_text(struct sink *sink, const char *text, int width, char pad,
                     bool left) {
  uint32_t length = 0;
  while (text[length])
    length++;
  int padding = width > (int)length ? width - (int)length : 0;
  if (!left)
    while (padding-- > 0)
      put(sink, pad);
  for (uint32_t i = 0; i < length; i++)
    put(sink, text[i]);
  if (left)
    while (padding-- > 0)
      put(sink, ' ');
}

static void put_number(struct sink *sink, uint64_t value, unsigned base,
                       bool negative, int width, char pad, bool left) {
  char digits[24];
  int count = 0;
  do {
    unsigned digit = (unsigned)(value % base);
    digits[count++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
    value /= base;
  } while (value);
  char text[26];
  int length = 0;
  if (negative)
    text[length++] = '-';
  while (count)
    text[length++] = digits[--count];
  text[length] = '\0';
  if (negative && pad == '0' && !left) {
    put(sink, '-');
    put_text(sink, text + 1, width - 1, pad, left);
    return;
  }
  put_text(sink, text, width, pad, left);
}

int32_t kvformat(char *out, uint32_t capacity, const char *format,
                 va_list args) {
  struct sink sink = {out, capacity, 0};
  for (const char *p = format; *p; p++) {
    if (*p != '%') {
      put(&sink, *p);
      continue;
    }
    p++;
    bool left = false;
    char pad = ' ';
    for (;; p++) {
      if (*p == '-')
        left = true;
      else if (*p == '0')
        pad = '0';
      else
        break;
    }
    int width = 0;
    while (*p >= '0' && *p <= '9')
      width = width * 10 + (*p++ - '0');
    int longs = 0;
    while (*p == 'l') {
      longs++;
      p++;
    }
    char spec = *p;
    if (!spec)
      break;
    switch (spec) {
    case '%':
      put(&sink, '%');
      break;
    case 'c':
      put(&sink, (char)va_arg(args, int));
      break;
    case 's': {
      const char *text = va_arg(args, const char *);
      put_text(&sink, text ? text : "(null)", width, ' ', left);
      break;
    }
    case 'd':
    case 'i': {
      int64_t value = longs ? va_arg(args, int64_t) : va_arg(args, int);
      bool negative = value < 0;
      put_number(&sink, negative ? (uint64_t)0 - (uint64_t)value : (uint64_t)value,
                 10, negative, width, pad, left);
      break;
    }
    case 'u':
    case 'x':
    case 'X': {
      uint64_t value = longs ? va_arg(args, uint64_t) : va_arg(args, unsigned);
      put_number(&sink, value, spec == 'u' ? 10 : 16, false, width, pad, left);
      break;
    }
    default:
      put(&sink, '%');
      put(&sink, spec);
      break;
    }
  }
  if (capacity) {
    uint32_t end = sink.length < capacity ? sink.length : capacity - 1;
    out[end] = '\0';
  }
  return (int32_t)sink.length;
}

int32_t kformat(char *out, uint32_t capacity, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int32_t length = kvformat(out, capacity, format, args);
  va_end(args);
  return length;
}
