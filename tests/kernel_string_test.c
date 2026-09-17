#include "lib/string.h"

#include <assert.h>
#include <stdio.h>

static void test_lengths_and_comparison(void){
    assert(strlen("") == 0);
    assert(strlen("PureC") == 5);
    assert(strcmp("same", "same") == 0);
    assert(strcmp("abc", "abd") < 0);
    assert(strcmp("abd", "abc") > 0);
    assert(strncmp("kernel", "keeper", 2) == 0);
    assert(strncmp("kernel", "keeper", 3) > 0);
    assert(strncmp("a", "ab", 8) < 0);
}

static void test_memory_operations(void){
    unsigned char source[] = {0, 1, 2, 127, 128, 255};
    unsigned char destination[sizeof(source)] = {0};
    assert(memcpy(destination, source, sizeof(source)) == destination);
    assert(memcmp(destination, source, sizeof(source)) == 0);

    assert(memset(destination, 0xa5, sizeof(destination)) == destination);
    for(size_t i = 0; i < sizeof(destination); i++)
        assert(destination[i] == 0xa5);

    char forward[] = "abcdef";
    assert(memmove(forward, forward + 2, 5) == forward);
    assert(memcmp(forward, "cdef", 5) == 0);

    char backward[8] = "abcdef";
    assert(memmove(backward + 2, backward, 5) == backward + 2);
    assert(memcmp(backward, "ababcde", 7) == 0);
}

static void test_string_copy_and_append(void){
    char buffer[16];
    assert(strcpy(buffer, "Pure") == buffer);
    assert(strcat(buffer, "C") == buffer);
    assert(strcmp(buffer, "PureC") == 0);

    char padded[6] = {'x', 'x', 'x', 'x', 'x', 'x'};
    assert(strncpy(padded, "OS", sizeof(padded)) == padded);
    assert(padded[0] == 'O' && padded[1] == 'S');
    for(size_t i = 2; i < sizeof(padded); i++) assert(padded[i] == '\0');

    char truncated[3] = {0};
    assert(strncpy(truncated, "kernel", sizeof(truncated)) == truncated);
    assert(memcmp(truncated, "ker", sizeof(truncated)) == 0);
}

int main(void){
    test_lengths_and_comparison();
    test_memory_operations();
    test_string_copy_and_append();
    puts("Kernel string tests passed");
    return 0;
}
