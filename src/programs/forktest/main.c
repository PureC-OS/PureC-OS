#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

static int counter = 100;

int main(void) {
    int local = 7;
    pid_t pid = fork();
    if (pid < 0) {
        printf("forktest: fork failed\n");
        return 1;
    }
    if (pid == 0) {
        counter += 1;
        local += 1;
        printf("forktest child: pid=%d counter=%d local=%d\n", (int)getpid(), counter, local);
        return 42;
    }
    int status = 0;
    pid_t waited = waitpid(pid, &status, 0);
    int ok = waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 42 &&
             counter == 100 && local == 7;
    printf("forktest parent: child=%d waited=%d code=%d counter=%d local=%d %s\n",
           (int)pid, (int)waited, WEXITSTATUS(status), counter, local, ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}
