#include "kernel/process/program_alias.h"

#include <assert.h>
#include <stdio.h>

static void expect_alias(const char *path){
    const char *resolved = NULL;
    assert(program_alias_resolve(path, &resolved));
    assert(resolved != NULL);
    assert(resolved[0] == '/' && resolved[1] == 'b');
}

static void expect_no_alias(const char *path){
    const char *sentinel = "unchanged";
    const char *resolved = sentinel;
    assert(!program_alias_resolve(path, &resolved));
    assert(resolved == sentinel);
}

int main(void){
    expect_alias("disks");
    expect_alias("/bin/debug");
    expect_alias("/bin/program/systeminfo");
    expect_alias("/bin/program/mkfs.fat32");

    expect_no_alias("");
    expect_no_alias("unknown");
    expect_no_alias("/usr/bin/debug");
    expect_no_alias("relative/debug");
    expect_no_alias("/bin/program/");
    assert(!program_alias_resolve(NULL, NULL));

    puts("Program alias tests passed");
    return 0;
}
