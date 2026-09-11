#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FILES_PATH_CAPACITY 256

bool files_path_join(char *result, uint32_t capacity,
                     const char *directory, const char *name);
bool files_path_parent(char *path, uint32_t capacity);
