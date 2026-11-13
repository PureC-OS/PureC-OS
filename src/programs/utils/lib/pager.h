#pragma once
#include <stdbool.h>
#include <stdio.h>

struct pager_text {
    char **lines;
    int count;
    int cap;
};

void pager_add(struct pager_text *t, const char *line);
void pager_add_file(struct pager_text *t, FILE *in);
void pager_free(struct pager_text *t);
int pager_show(struct pager_text *t, const char *title, bool always);
