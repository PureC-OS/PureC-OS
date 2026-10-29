#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int failures;
static volatile int usr1_seen;

static void check(const char *name, int ok) {
    printf("proctest %-28s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

static void on_usr1(int sig) { usr1_seen = sig; }

static int read_all(int fd, char *out, size_t size) {
    size_t used = 0;
    for (;;) {
        ssize_t got = read(fd, out + used, size - 1 - used);
        if (got <= 0) break;
        used += (size_t)got;
        if (used + 1 >= size) break;
    }
    out[used] = '\0';
    return (int)used;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--child-env") == 0) {
        const char *foo = getenv("FOO");
        return (argc == 4 && strcmp(argv[2], "two words") == 0 && foo &&
                strcmp(foo, "bar") == 0)
                   ? 7
                   : 1;
    }

    pid_t pid = fork();
    if (pid == 0) _exit(42);
    int status = 0;
    check("fork+waitpid exit code", waitpid(pid, &status, 0) == pid &&
                                        WIFEXITED(status) && WEXITSTATUS(status) == 42);

    int fds[2];
    check("pipe", pipe(fds) == 0);
    pid = fork();
    if (pid == 0) {
        close(fds[0]);
        write(fds[1], "ping", 4);
        _exit(0);
    }
    close(fds[1]);
    char text[128];
    read_all(fds[0], text, sizeof(text));
    close(fds[0]);
    waitpid(pid, &status, 0);
    check("pipe parent<-child", strcmp(text, "ping") == 0);

    pipe(fds);
    pid = fork();
    if (pid == 0) {
        dup2(fds[1], 1);
        close(fds[0]);
        close(fds[1]);
        char *args[] = {"hello", "redirected", 0};
        execvp("hello", args);
        _exit(127);
    }
    close(fds[1]);
    read_all(fds[0], text, sizeof(text));
    close(fds[0]);
    waitpid(pid, &status, 0);
    check("dup2+execvp stdout capture", strcmp(text, "hello redirected from PureC OS\n") == 0);

    pid = fork();
    if (pid == 0) {
        char *args[] = {"proctest", "--child-env", "two words", "x", 0};
        char *env[] = {"FOO=bar", "PATH=/usr/bin:/bin", 0};
        execve("/usr/bin/proctest", args, env);
        _exit(127);
    }
    waitpid(pid, &status, 0);
    check("execve argv+envp", WIFEXITED(status) && WEXITSTATUS(status) == 7);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_usr1;
    sigaction(SIGUSR1, &action, 0);
    kill(getpid(), SIGUSR1);
    check("signal handler (self kill)", usr1_seen == SIGUSR1);

    pid = fork();
    if (pid == 0) {
        for (;;) pause();
    }
    sleep(0);
    kill(pid, SIGTERM);
    waitpid(pid, &status, 0);
    check("SIGTERM default action", WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);

    pid = fork();
    if (pid == 0) {
        volatile int *bad = (volatile int *)0;
        *bad = 1;
        _exit(0);
    }
    waitpid(pid, &status, 0);
    check("SIGSEGV on null write", WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);

    pid_t first = fork();
    if (first == 0) _exit(1);
    pid_t second = fork();
    if (second == 0) _exit(2);
    int reaped = 0;
    int sum = 0;
    for (int i = 0; i < 2; i++) {
        pid_t got = waitpid(-1, &status, 0);
        if (got == first || got == second) {
            reaped++;
            sum += WEXITSTATUS(status);
        }
    }
    check("waitpid(-1) two children", reaped == 2 && sum == 3);
    check("ECHILD when none left", waitpid(-1, &status, 0) == -1 && errno == ECHILD);

    pipe(fds);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    char *spawn_args[] = {"hello", "spawn", 0};
    int rc = posix_spawnp(&pid, "hello", &actions, 0, spawn_args, environ);
    close(fds[1]);
    read_all(fds[0], text, sizeof(text));
    close(fds[0]);
    if (rc == 0) waitpid(pid, &status, 0);
    posix_spawn_file_actions_destroy(&actions);
    check("posix_spawnp+file actions", rc == 0 && strcmp(text, "hello spawn from PureC OS\n") == 0);
    check("posix_spawn missing file", posix_spawn(&pid, "/nonexistent/prog", 0, 0, spawn_args, environ) != 0);

    check("chdir/getcwd", chdir("/") == 0 && getcwd(text, sizeof(text)) && strcmp(text, "/") == 0);

    int sys = system("exit 3");
    check("system(sh -c)", WIFEXITED(sys) && WEXITSTATUS(sys) == 3);
    FILE *p = popen("echo popen", "r");
    if (p) {
        fgets(text, sizeof(text), p);
        int closed = pclose(p);
        check("popen read", strcmp(text, "popen\n") == 0 && WIFEXITED(closed));
    } else {
        check("popen read", 0);
    }

    printf("proctest done failures=%d\n", failures);
    return failures ? 1 : 0;
}
