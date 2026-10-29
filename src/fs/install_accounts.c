#include "install_accounts.h"
#include "types/fs_types.h"

static uint32_t text_length(const char *text) {
  uint32_t length = 0;
  while (text[length])
    length++;
  return length;
}

static bool bounded(const char *text, uint32_t capacity) {
  for (uint32_t i = 0; i < capacity; i++)
    if (!text[i])
      return true;
  return false;
}

static bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool install_account_name_valid(const char *name) {
  if (!name || !bounded(name, INSTALL_USER_MAX))
    return false;
  uint32_t length = text_length(name);
  if (length == 0)
    return false;
  if (!is_lower(name[0]) && name[0] != '_')
    return false;
  for (uint32_t i = 1; i < length; i++)
    if (!is_lower(name[i]) && !is_digit(name[i]) && name[i] != '_' &&
        name[i] != '-')
      return false;
  return true;
}

bool install_account_hostname_valid(const char *name) {
  if (!name || !bounded(name, INSTALL_HOSTNAME_MAX))
    return false;
  uint32_t length = text_length(name);
  if (length == 0 || name[0] == '-' || name[length - 1] == '-')
    return false;
  for (uint32_t i = 0; i < length; i++)
    if (!is_lower(name[i]) && !is_digit(name[i]) && name[i] != '-' &&
        name[i] != '.')
      return false;
  return true;
}

bool install_account_hash_valid(const char *hash) {
  if (!hash || !bounded(hash, INSTALL_HASH_MAX))
    return false;
  uint32_t length = text_length(hash);
  if (length < 12 || hash[0] != '$' || hash[1] != '6' || hash[2] != '$')
    return false;
  for (uint32_t i = 0; i < length; i++)
    if (hash[i] == ':' || hash[i] == '\n' || hash[i] == '\r' ||
        hash[i] == ' ')
      return false;
  return true;
}

bool install_account_gecos_valid(const char *text) {
  if (!text || !bounded(text, INSTALL_GECOS_MAX))
    return false;
  for (uint32_t i = 0; text[i]; i++)
    if (text[i] == ':' || text[i] == '\n' || text[i] == '\r' ||
        (unsigned char)text[i] < 0x20)
      return false;
  return true;
}

bool install_accounts_valid(const struct install_account_request *request) {
  if (!request || !request->present)
    return false;
  if (!install_account_hostname_valid(request->hostname))
    return false;
  if (!install_account_hash_valid(request->root_hash))
    return false;
  if (!install_account_name_valid(request->user_name) ||
      request->user_name[0] == '_')
    return false;
  const char *name = request->user_name;
  if (name[0] == 'r' && name[1] == 'o' && name[2] == 'o' && name[3] == 't' &&
      !name[4])
    return false;
  if (!install_account_gecos_valid(request->user_gecos))
    return false;
  if (!install_account_hash_valid(request->user_hash))
    return false;
  return request->user_uid >= INSTALL_FIRST_UID && request->user_uid < 60000;
}

struct writer {
  char *buffer;
  uint32_t capacity;
  uint32_t length;
  bool failed;
};

static void put_text(struct writer *w, const char *text) {
  for (uint32_t i = 0; text[i]; i++) {
    if (w->length + 1 >= w->capacity) {
      w->failed = true;
      return;
    }
    w->buffer[w->length++] = text[i];
  }
  w->buffer[w->length] = '\0';
}

static void put_number(struct writer *w, uint32_t value) {
  char digits[12];
  uint32_t count = 0;
  do {
    digits[count++] = (char)('0' + value % 10u);
    value /= 10u;
  } while (value);
  char text[12];
  for (uint32_t i = 0; i < count; i++)
    text[i] = digits[count - 1 - i];
  text[count] = '\0';
  put_text(w, text);
}

static void start(struct writer *w, char *buffer, uint32_t capacity) {
  w->buffer = buffer;
  w->capacity = capacity;
  w->length = 0;
  w->failed = false;
  buffer[0] = '\0';
}

int32_t install_accounts_render(const struct install_account_request *request,
                                struct install_account_files *out) {
  if (!out || !install_accounts_valid(request))
    return FS_ERROR_INVALID;
  struct writer w;

  start(&w, out->passwd, sizeof(out->passwd));
  put_text(&w, "root:x:0:0:root:/root:" INSTALL_SHELL_DEFAULT "\n");
  put_text(&w, request->user_name);
  put_text(&w, ":x:");
  put_number(&w, request->user_uid);
  put_text(&w, ":");
  put_number(&w, INSTALL_USERS_GID);
  put_text(&w, ":");
  put_text(&w, request->user_gecos);
  put_text(&w, ":/home/");
  put_text(&w, request->user_name);
  put_text(&w, ":" INSTALL_SHELL_DEFAULT "\n");
  if (w.failed)
    return FS_ERROR_NO_SPACE;

  start(&w, out->shadow, sizeof(out->shadow));
  put_text(&w, "root:");
  put_text(&w, request->root_hash);
  put_text(&w, ":::::::\n");
  put_text(&w, request->user_name);
  put_text(&w, ":");
  put_text(&w, request->user_hash);
  put_text(&w, ":::::::\n");
  if (w.failed)
    return FS_ERROR_NO_SPACE;

  start(&w, out->group, sizeof(out->group));
  put_text(&w, "root:x:0:\nusers:x:");
  put_number(&w, INSTALL_USERS_GID);
  put_text(&w, ":");
  put_text(&w, request->user_name);
  put_text(&w, "\n");
  if (w.failed)
    return FS_ERROR_NO_SPACE;

  start(&w, out->hostname, sizeof(out->hostname));
  put_text(&w, request->hostname);
  put_text(&w, "\n");
  if (w.failed)
    return FS_ERROR_NO_SPACE;

  start(&w, out->home, sizeof(out->home));
  put_text(&w, "/home/");
  put_text(&w, request->user_name);
  if (w.failed)
    return FS_ERROR_NO_SPACE;
  return 0;
}
