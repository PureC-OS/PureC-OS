// PureC hosted hello-world: canonical example for standard C programs.
// Builds against the hosted libc (src/libc/include/hosted/) and links
// bin/lib/crt0.o, which provides _start and calls this main().
// Run on PureC OS as: /bin/program/hello [name...]

#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc > 1) {
        printf("hello");
        for (int i = 1; i < argc; i++) {
            printf(" %s", argv[i]);
        }
        printf(" from PureC OS\n");
    } else {
        printf("hello PureC OS\n");
    }
    return 0;
}
