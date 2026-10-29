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

    int wfd = open("/tmp/ofd_test", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    check("open O_WRONLY|O_CREAT|O_TRUNC", wfd >= 0 && write(wfd, "abcdefgh", 8) == 8);
    close(wfd);
    int rfd = open("/tmp/ofd_test", O_RDONLY);
    int rdup = dup(rfd);
    char two[4] = {0};
    char three[4] = {0};
    read(rfd, two, 3);
    read(rdup, three, 3);
    check("dup shares file offset", strcmp(two, "abc") == 0 && strcmp(three, "def") == 0);
    pid = fork();
    if (pid == 0) {
        char c;
        _exit(read(rfd, &c, 1) == 1 && c == 'g' ? 0 : 1);
    }
    waitpid(pid, &status, 0);
    char last = 0;
    read(rfd, &last, 1);
    check("fork shares file offset", WIFEXITED(status) && WEXITSTATUS(status) == 0 && last == 'h');
    close(rfd);
    close(rdup);

    int rw = open("/tmp/ofd_test", O_RDWR);
    check("O_RDWR open", rw >= 0);
    lseek(rw, 2, SEEK_SET);
    write(rw, "XY", 2);
    lseek(rw, 0, SEEK_SET);
    char all[16] = {0};
    ssize_t got = read(rw, all, sizeof(all) - 1);
    check("write at offset in middle", got == 8 && strcmp(all, "abXYefgh") == 0);
    check("ftruncate shrink", ftruncate(rw, 4) == 0);
    lseek(rw, 0, SEEK_SET);
    memset(all, 0, sizeof(all));
    got = read(rw, all, sizeof(all) - 1);
    check("ftruncate result", got == 4 && strcmp(all, "abXY") == 0);
    check("lseek SEEK_END", lseek(rw, 0, SEEK_END) == 4);
    close(rw);

    FILE *fp = fopen("/tmp/ofd_test", "r+");
    check("fopen r+", fp != 0);
    if (fp) {
        fseek(fp, 1, SEEK_SET);
        fputc('Q', fp);
        fseek(fp, 0, SEEK_SET);
        fgets(text, sizeof(text), fp);
        check("FILE r+ overwrite via fd", strcmp(text, "aQXY") == 0);
        fclose(fp);
    }

    pipe(fds);
    int fl = fcntl(fds[0], F_GETFL);
    fcntl(fds[0], F_SETFL, fl | O_NONBLOCK);
    char probe;
    check("O_NONBLOCK pipe EAGAIN", read(fds[0], &probe, 1) == -1 && errno == EAGAIN);
    close(fds[0]);
    close(fds[1]);
    unlink("/tmp/ofd_test");

    printf("proctest done failures=%d\n", failures);
    return failures ? 1 : 0;
}
