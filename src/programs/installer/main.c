#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include "../accounts/accounts.h"

#define MAX_DISKS 16
#define PKG_CONF "/etc/pkg/pkg.conf"
#define USERSPACE_PKG "userspace"

static struct install_account_request accounts;
static struct storage_device_info disk;
static bool graphical;
static char userspace_url[256];
static char extra_packages[256];

static void flush_line(void)
{
    int c;
    while ((c = getchar()) != EOF && c != '\n') {
    }
}

static bool ask(const char *prompt, const char *def, char *out, size_t cap)
{
    if (def && *def)
        printf("%s [%s]: ", prompt, def);
    else
        printf("%s: ", prompt);
    fflush(stdout);
    if (!fgets(out, (int)cap, stdin)) {
        out[0] = 0;
        return false;
    }
    size_t n = strlen(out);
    if (n && out[n - 1] == '\n')
        out[--n] = 0;
    else if (n + 1 == cap)
        flush_line();
    while (n && (out[n - 1] == ' ' || out[n - 1] == '\r'))
        out[--n] = 0;
    char *s = out;
    while (*s == ' ')
        s++;
    memmove(out, s, strlen(s) + 1);
    if (!*out && def)
        snprintf(out, cap, "%s", def);
    return true;
}

static bool yes_no(const char *prompt, bool def)
{
    char line[16];
    for (;;) {
        printf("%s [%s]: ", prompt, def ? "Y/n" : "y/N");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin))
            return def;
        if (line[0] == '\n')
            return def;
        if (line[0] == 'y' || line[0] == 'Y')
            return true;
        if (line[0] == 'n' || line[0] == 'N')
            return false;
    }
}

static void echo_input(bool on)
{
    struct termios t;
    if (tcgetattr(0, &t) != 0)
        return;
    if (on)
        t.c_lflag |= ECHO;
    else
        t.c_lflag &= ~(tcflag_t)ECHO;
    tcsetattr(0, TCSANOW, &t);
}

static void wipe(void *p, size_t n)
{
    volatile unsigned char *b = p;
    while (n--)
        *b++ = 0;
}

static bool ask_password(const char *who, char *hash_out, size_t cap)
{
    char a[128], b[128];
    for (int tries = 0; tries < 5; tries++) {
        printf("Password for %s: ", who);
        fflush(stdout);
        echo_input(false);
        bool ok = fgets(a, sizeof(a), stdin) != NULL;
        printf("\nRepeat the password: ");
        fflush(stdout);
        ok = ok && fgets(b, sizeof(b), stdin) != NULL;
        echo_input(true);
        putchar('\n');
        if (!ok)
            return false;
        a[strcspn(a, "\r\n")] = 0;
        b[strcspn(b, "\r\n")] = 0;
        if (strcmp(a, b) != 0) {
            printf("  The passwords do not match.\n");
            continue;
        }
        if (strlen(a) < ACCOUNT_PASSWORD_MIN) {
            printf("  The password is empty.\n");
            continue;
        }
        bool hashed = account_hash_password(a, hash_out, (uint32_t)cap);
        wipe(a, sizeof(a));
        wipe(b, sizeof(b));
        if (!hashed) {
            printf("  Cannot hash the password.\n");
            return false;
        }
        return true;
    }
    return false;
}

static void human(uint64_t bytes, char *out, size_t cap)
{
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024 && u < 4) {
        v /= 1024;
        u++;
    }
    snprintf(out, cap, "%.1f %s", v, units[u]);
}

static const char *transport_name(uint8_t t)
{
    switch (t) {
    case 1: return "ATA";
    case 2: return "SATA";
    case 3: return "USB";
    case 4: return "USB";
    default: return "?";
    }
}

static bool choose_disk(void)
{
    struct storage_device_info list[MAX_DISKS];
    int32_t n = pc_list_disks(list, MAX_DISKS);
    if (n <= 0) {
        printf("No disks found.\n");
        return false;
    }
    int usable[MAX_DISKS], count = 0;
    printf("\n  #  DISK   SIZE        BUS   MODEL\n");
    for (int i = 0; i < n && i < MAX_DISKS; i++) {
        if (!list[i].operational || !list[i].writable || list[i].sector_size != 512)
            continue;
        char size[32];
        human(list[i].sector_count * list[i].sector_size, size, sizeof(size));
        usable[count++] = i;
        printf("  %d  %-6s %-11s %-5s %s\n", count, list[i].name, size, transport_name(list[i].transport),
               list[i].model);
    }
    if (!count) {
        printf("No writable disk with 512-byte sectors.\n");
        return false;
    }
    char line[32];
    for (;;) {
        ask("Install to disk number", count == 1 ? "1" : NULL, line, sizeof(line));
        int pick = atoi(line);
        if (pick >= 1 && pick <= count) {
            disk = list[usable[pick - 1]];
            break;
        }
    }
    char size[32];
    human(disk.sector_count * disk.sector_size, size, sizeof(size));
    printf("\nEVERYTHING on %s (%s, %s) will be erased.\n", disk.name, disk.model, size);
    ask("Type the disk name to confirm", NULL, line, sizeof(line));
    const char *bare = disk.name;
    if (!strncmp(bare, "/dev/", 5))
        bare += 5;
    const char *typed = !strncmp(line, "/dev/", 5) ? line + 5 : line;
    if (strcmp(typed, bare) != 0) {
        printf("Not confirmed.\n");
        return false;
    }
    return true;
}

static bool valid_user_name(const char *s)
{
    if (!*s || strlen(s) >= INSTALL_USER_MAX || !(islower((unsigned char)*s) || *s == '_'))
        return false;
    for (const char *p = s; *p; p++)
        if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p) && *p != '_' && *p != '-')
            return false;
    return strcmp(s, "root") != 0;
}

static bool valid_host(const char *s)
{
    if (!*s || strlen(s) >= INSTALL_HOSTNAME_MAX || *s == '-')
        return false;
    for (const char *p = s; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '-')
            return false;
    return true;
}

static bool collect_accounts(void)
{
    memset(&accounts, 0, sizeof(accounts));
    accounts.present = 1;
    char line[96];
    do {
        ask("Hostname", "purec", line, sizeof(line));
    } while (!valid_host(line) && printf("  Letters, digits and '-' only.\n"));
    snprintf(accounts.hostname, sizeof(accounts.hostname), "%s", line);
    ask("Time zone", "UTC", line, sizeof(line));
    snprintf(accounts.timezone, sizeof(accounts.timezone), "%s", line);
    printf("\n");
    if (!ask_password("root", accounts.root_hash, sizeof(accounts.root_hash)))
        return false;
    while (accounts.user_count < INSTALL_USERS_MAX &&
           yes_no(accounts.user_count ? "\nAdd another user?" : "\nAdd a user?", accounts.user_count == 0)) {
        struct install_user *u = &accounts.users[accounts.user_count];
        memset(u, 0, sizeof(*u));
        for (;;) {
            ask("User name", NULL, line, sizeof(line));
            bool dup = false;
            for (uint32_t i = 0; i < accounts.user_count; i++)
                if (!strcmp(accounts.users[i].name, line))
                    dup = true;
            if (valid_user_name(line) && !dup)
                break;
            printf("  Lower-case letters, digits, '_' and '-', starting with a letter, not root, not repeated.\n");
        }
        snprintf(u->name, sizeof(u->name), "%s", line);
        ask("Full name", "", line, sizeof(line));
        snprintf(u->gecos, sizeof(u->gecos), "%s", line);
        if (!ask_password(u->name, u->hash, sizeof(u->hash)))
            return false;
        u->admin = yes_no("Administrator (sudo, group wheel)?", true) ? 1 : 0;
        u->uid = INSTALL_FIRST_UID + accounts.user_count;
        accounts.user_count++;
    }
    return true;
}

static bool userspace_trusted(void)
{
    FILE *f = fopen(PKG_CONF, "r");
    if (!f)
        return false;
    char line[320];
    bool in_trusted = false, found = false;
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t')
            s++;
        if (*s == '[')
            in_trusted = !strncmp(s, "[trusted]", 9);
        else if (in_trusted && !strncmp(s, USERSPACE_PKG, strlen(USERSPACE_PKG))) {
            char *rest = s + strlen(USERSPACE_PKG);
            while (*rest == ' ' || *rest == '\t')
                rest++;
            if (*rest == '=')
                found = true;
        }
    }
    fclose(f);
    return found;
}

static void choose_profile(void)
{
    printf("\nProfile:\n  1  Console only\n  2  Graphical (downloads '%s' from GitHub, needs network)\n", USERSPACE_PKG);
    char line[256];
    ask("Profile", "1", line, sizeof(line));
    graphical = line[0] == '2';
    if (graphical && !userspace_trusted()) {
        printf("'%s' is not in [trusted] of " PKG_CONF ".\n", USERSPACE_PKG);
        for (;;) {
            ask("GitHub repository of the user space (https://github.com/OWNER/REPO, empty = console only)", "", line,
                sizeof(line));
            if (!line[0]) {
                graphical = false;
                break;
            }
            if (!strncmp(line, "https://github.com/", 19) && strchr(line + 19, '/')) {
                snprintf(userspace_url, sizeof(userspace_url), "%s", line);
                break;
            }
            printf("  Give a https://github.com/OWNER/REPO address.\n");
        }
    }
    ask("Extra packages (names from " PKG_CONF ", space separated)", "", extra_packages, sizeof(extra_packages));
}

static bool summary(void)
{
    char size[32];
    human(disk.sector_count * disk.sector_size, size, sizeof(size));
    printf("\n== Summary ==\n");
    printf("  Disk      : %s %s (%s) - erased, GPT: EFI + ext2 root\n", disk.name, disk.model, size);
    printf("  Hostname  : %s\n  Time zone : %s\n", accounts.hostname, accounts.timezone);
    printf("  Users     :");
    if (!accounts.user_count)
        printf(" only root");
    for (uint32_t i = 0; i < accounts.user_count; i++)
        printf(" %s%s", accounts.users[i].name, accounts.users[i].admin ? "(wheel)" : "");
    printf("\n  Profile   : %s\n", graphical ? "graphical" : "console");
    if (userspace_url[0])
        printf("  User space: %s\n", userspace_url);
    if (extra_packages[0])
        printf("  Packages  : %s\n", extra_packages);
    return yes_no("\nStart the installation?", false);
}

static void bar(uint32_t progress, const char *stage)
{
    int filled = (int)(progress > 100 ? 100 : progress) * 30 / 100;
    char b[31];
    for (int i = 0; i < 30; i++)
        b[i] = i < filled ? '#' : '.';
    b[30] = 0;
    printf("\r[%s] %3u%% %-40.40s", b, progress, stage);
    fflush(stdout);
}

static bool run_kernel_install(void)
{
    int32_t rc = pc_install_start_accounts(disk.name, disk.serial, FS_TYPE_EXT2, &accounts);
    wipe(&accounts.root_hash, sizeof(accounts.root_hash));
    if (rc < 0) {
        printf("The kernel refused to start the installation (error %d).\n", rc);
        if (rc == -10)
            printf("Run the installer as /sbin/installer: only that path may write disks.\n");
        return false;
    }
    struct install_status st;
    char last[INSTALL_STAGE_CAPACITY] = "";
    for (;;) {
        if (!pc_install_status(&st)) {
            usleep(200000);
            continue;
        }
        if (strcmp(st.stage, last) != 0 || st.state >= 2) {
            snprintf(last, sizeof(last), "%s", st.stage);
            bar(st.progress, st.stage);
        }
        if (st.state == 2) {
            printf("\n");
            return true;
        }
        if (st.state == 3) {
            printf("\nInstallation failed: %s (error %d)\n", st.stage, st.result);
            struct install_log log;
            if (pc_install_log(&log))
                for (uint32_t i = 0; i < log.count && i < INSTALL_LOG_CAPACITY; i++)
                    printf("  %3u%% %s\n", log.entries[i].progress, log.entries[i].stage);
            return false;
        }
        usleep(200000);
    }
}

static int run(char *const argv[])
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execv(argv[0], argv);
        fprintf(stderr, "cannot run %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static bool add_trusted(const char *name, const char *url)
{
    FILE *f = fopen(PKG_CONF, "r");
    bool has_section = false;
    if (f) {
        char line[320];
        while (fgets(line, sizeof(line), f))
            if (!strncmp(line, "[trusted]", 9))
                has_section = true;
        fclose(f);
    }
    f = fopen(PKG_CONF, "a");
    if (!f)
        return false;
    if (!has_section)
        fprintf(f, "\n[trusted]\n");
    fprintf(f, "%s = %s\n", name, url);
    fclose(f);
    return true;
}

static bool install_packages(void)
{
    char *argv[48];
    int n = 0;
    static char names[256];
    snprintf(names, sizeof(names), "%s", extra_packages);
    argv[n++] = "/usr/bin/pkg";
    argv[n++] = "-S";
    argv[n++] = "--noconfirm";
    argv[n++] = "--needed";
    if (graphical)
        argv[n++] = USERSPACE_PKG;
    for (char *w = strtok(names, " \t,"); w && n < 46; w = strtok(NULL, " \t,"))
        argv[n++] = w;
    argv[n] = NULL;
    if (n == 4)
        return true;
    printf("\n== Packages ==\n");
    char *refresh[] = {"/usr/bin/pkg", "-Sy", NULL};
    if (run(refresh) != 0)
        printf("Some trusted repositories could not be checked.\n");
    return run(argv) == 0;
}

static void write_default_target(const char *target)
{
    mkdir("/etc/init", 0755);
    FILE *f = fopen("/etc/init/default.target", "w");
    if (f) {
        fprintf(f, "%s\n", target);
        fclose(f);
    }
}

int main(void)
{
    if (getuid() != 0) {
        fprintf(stderr, "installer: run it as root\n");
        return 1;
    }
    printf("PureC OS installer\n==================\n"
           "The system is copied from this live medium to a disk. Ctrl+C stops it\n"
           "before the installation starts.\n");
    if (!choose_disk())
        return 1;
    if (!collect_accounts())
        return 1;
    choose_profile();
    if (!summary()) {
        printf("Nothing was changed.\n");
        return 1;
    }
    printf("\n== Installing ==\n");
    if (!run_kernel_install())
        return 1;
    bool warnings = false;
    printf("\n== Boot loader ==\n");
    char *mkinit[] = {"/sbin/mkinitcpio", "-u", NULL};
    if (run(mkinit) != 0) {
        warnings = true;
        printf("WARNING: mkinitcpio failed, the installed system may start the live initramfs\n"
               "instead of the disk. After booting, run: mkinitcpio -u\n");
    }
    if (userspace_url[0] && !add_trusted(USERSPACE_PKG, userspace_url)) {
        warnings = true;
        printf("WARNING: cannot add %s to " PKG_CONF "\n", USERSPACE_PKG);
    }
    if (graphical || extra_packages[0]) {
        if (!install_packages()) {
            warnings = true;
            printf("WARNING: packages were not installed. After booting run: pkg -Sy %s %s\n",
                   graphical ? USERSPACE_PKG : "", extra_packages);
            graphical = false;
        }
    }
    write_default_target(graphical ? "graphical.target" : "multi-user.target");
    sync();
    printf("\nInstallation %s. Remove the live medium and reboot.\n", warnings ? "finished with warnings" : "complete");
    if (yes_no("Reboot now?", true)) {
        char *rb[] = {"/sbin/reboot", NULL};
        run(rb);
    }
    return 0;
}
