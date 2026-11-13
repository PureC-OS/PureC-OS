#pragma once
#include "common.h"

int fo_copy_file(const char *from, const char *to, bool preserve);
int fo_copy_tree(const char *from, const char *to, bool preserve, bool verbose, bool no_clobber);
int fo_remove_tree(const char *path, bool verbose, bool force);
int fo_mkdirs(const char *path, unsigned int mode, bool verbose);
