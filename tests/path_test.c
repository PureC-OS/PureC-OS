#include "programs/files/path.h"
#include "programs/terminal/path.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint32_t pc_strlen(const char *text){
    uint32_t length = 0;
    while(text && text[length]) length++;
    return length;
}

void pc_copy(char *destination, const char *source, uint32_t capacity){
    if(!destination || !capacity) return;
    uint32_t i = 0;
    while(source && source[i] && i + 1 < capacity){
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

static void expect_normalized(const char *base, const char *path,
                              const char *expected){
    char output[128] = "untouched";
    assert(shell_path_normalize(base, path, output, sizeof(output)));
    assert(strcmp(output, expected) == 0);
}

static void test_shell_paths(void){
    expect_normalized("/home/user", "docs/./notes", "/home/user/docs/notes");
    expect_normalized("/home/user", "../tmp", "/home/tmp");
    expect_normalized("/", "../../etc//config", "/etc/config");
    expect_normalized("/ignored", "/var/../bin", "/bin");
    expect_normalized("/home", "", "/home");

    char small[5] = "safe";
    assert(!shell_path_normalize("/", "hello", small, sizeof(small)));
    assert(!shell_path_normalize("relative", "file", small, sizeof(small)));
    assert(!shell_path_normalize(NULL, "file", small, sizeof(small)));
}

static void test_file_paths(void){
    char path[64];
    assert(files_path_join(path, sizeof(path), "/", "readme.txt"));
    assert(strcmp(path, "/readme.txt") == 0);
    assert(files_path_join(path, sizeof(path), "/home", "user"));
    assert(strcmp(path, "/home/user") == 0);
    assert(!files_path_join(path, 6, "/home", "user"));
    assert(!files_path_join(path, sizeof(path), "/home", ""));

    strcpy(path, "/home/user/docs/");
    assert(files_path_parent(path, sizeof(path)));
    assert(strcmp(path, "/home/user") == 0);
    assert(files_path_parent(path, sizeof(path)));
    assert(strcmp(path, "/home") == 0);
    assert(files_path_parent(path, sizeof(path)));
    assert(strcmp(path, "/") == 0);
    assert(!files_path_parent(path, sizeof(path)));
}

int main(void){
    test_shell_paths();
    test_file_paths();
    puts("Path tests passed");
    return 0;
}
