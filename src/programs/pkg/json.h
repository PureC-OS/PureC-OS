#pragma once
#include <stddef.h>

const char *json_ws(const char *p);
const char *json_skip(const char *p);
int json_string(const char *p, char *out, size_t cap);
long long json_number(const char *p);
int json_object_next(const char **cursor, char *key, size_t key_cap, const char **value);
int json_array_next(const char **cursor, const char **value);
const char *json_get(const char *object, const char *name);
