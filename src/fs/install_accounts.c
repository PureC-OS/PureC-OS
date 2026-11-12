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

static bool same_text(const char *a, const char *b) {
  uint32_t i = 0;
  while (a[i] && a[i] == b[i])
    i++;
  return a[i] == b[i];
}

bool install_account_user_valid(const struct install_user *user) {
  if (!user || !install_account_name_valid(user->name) || user->name[0] == '_')
    return false;
  if (same_text(user->name, "root"))
    return false;
  if (!bounded(user->gecos, INSTALL_GECOS_MAX) || !user->gecos[0] ||
      !install_account_gecos_valid(user->gecos))
    return false;
  if (!install_account_hash_valid(user->hash))
    return false;
  return user->uid >= INSTALL_FIRST_UID && user->uid < 60000;
}

bool install_accounts_valid(const struct install_account_request *request) {
  if (!request || !request->present)
    return false;
  if (!install_account_hostname_valid(request->hostname))
    return false;
  if (!install_account_hash_valid(request->root_hash))
    return false;
  if (request->user_count > INSTALL_USERS_MAX)
    return false;
  for (uint32_t i = 0; i < request->user_count; i++) {
    const struct install_user *user = &request->users[i];
    if (!install_account_user_valid(user))
      return false;
    for (uint32_t j = 0; j < i; j++)
      if (same_text(request->users[j].name, user->name) ||
          request->users[j].uid == user->uid)
        return false;
  }
  return true;
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
  uint32_t count = request->user_count;

  start(&w, out->passwd, sizeof(out->passwd));
  put_text(&w, "root:x:0:0:root:/root:" INSTALL_SHELL_DEFAULT "\n");
  for (uint32_t i = 0; i < count; i++) {
    const struct install_user *u = &request->users[i];
    put_text(&w, u->name);
    put_text(&w, ":x:");
    put_number(&w, u->uid);
    put_text(&w, ":");
    put_number(&w, INSTALL_USERS_GID);
    put_text(&w, ":");
    put_text(&w, u->gecos);
    put_text(&w, ":/home/");
    put_text(&w, u->name);
    put_text(&w, ":" INSTALL_SHELL_DEFAULT "\n");
  }
  if (w.failed)
    return FS_ERROR_NO_SPACE;

  start(&w, out->shadow, sizeof(out->shadow));
  put_text(&w, "root:");
  put_text(&w, request->root_hash);
  put_text(&w, ":::::::\n");
  for (uint32_t i = 0; i < count; i++) {
    put_text(&w, request->users[i].name);
    put_text(&w, ":");
    put_text(&w, request->users[i].hash);
    put_text(&w, ":::::::\n");
  }
  if (w.failed)
    return FS_ERROR_NO_SPACE;

  start(&w, out->group, sizeof(out->group));
  put_text(&w, "root:x:0:\nwheel:x:");
  put_number(&w, INSTALL_WHEEL_GID);
  put_text(&w, ":");
  bool first = true;
  for (uint32_t i = 0; i < count; i++) {
    if (!request->users[i].admin)
      continue;
    if (!first)
      put_text(&w, ",");
    put_text(&w, request->users[i].name);
    first = false;
  }
  put_text(&w, "\nusers:x:");
  put_number(&w, INSTALL_USERS_GID);
  put_text(&w, ":");
  for (uint32_t i = 0; i < count; i++) {
    if (i)
      put_text(&w, ",");
    put_text(&w, request->users[i].name);
  }
  put_text(&w, "\n");
  if (w.failed)
    return FS_ERROR_NO_SPACE;

  start(&w, out->hostname, sizeof(out->hostname));
  put_text(&w, request->hostname);
  put_text(&w, "\n");
  if (w.failed)
    return FS_ERROR_NO_SPACE;
  return 0;
}
