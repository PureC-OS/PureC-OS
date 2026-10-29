#include "accounts_core.h"
#include "purecrypt.h"

static bool field_equals(const char *line, const char *name) {
  uint32_t i = 0;
  while (name[i]) {
    if (line[i] != name[i])
      return false;
    i++;
  }
  return line[i] == ':';
}

bool shadow_hash_for(const char *shadow, const char *name, char *out,
                     uint32_t capacity) {
  if (!shadow || !name || !name[0] || !out || !capacity)
    return false;
  const char *line = shadow;
  while (*line) {
    if (*line != '#' && field_equals(line, name)) {
      const char *hash = line;
      while (*hash && *hash != ':' && *hash != '\n')
        hash++;
      if (*hash != ':')
        return false;
      hash++;
      uint32_t length = 0;
      while (hash[length] && hash[length] != ':' && hash[length] != '\n' &&
             hash[length] != '\r') {
        if (length + 1 >= capacity)
          return false;
        out[length] = hash[length];
        length++;
      }
      out[length] = '\0';
      return length > 0;
    }
    while (*line && *line != '\n')
      line++;
    if (*line == '\n')
      line++;
  }
  return false;
}

bool accounts_check(const char *shadow, const char *name,
                    const char *password) {
  char hash[PURECRYPT_HASH_MAX];
  if (!password || !shadow_hash_for(shadow, name, hash, sizeof(hash)))
    return false;
  if (hash[0] != '$' || hash[1] != '6' || hash[2] != '$')
    return false;
  return purecrypt_verify(password, hash) == 1;
}
