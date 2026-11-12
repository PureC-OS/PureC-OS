#include "../lib/common.h"

struct group_info { const char *title; const char *items; };

static const struct group_info groups[] = {
    {"Files", "ls cat cp mv rm mkdir rmdir touch chmod stat find tree du df install realpath basename dirname"},
    {"Text", "grep egrep fgrep head tail wc sort uniq cut tr tee cmp diff nl rev seq xxd less echo"},
    {"System", "ps kill free uptime date uname whoami id hostname env which sleep clear reboot poweroff shutdown halt"},
    {"Network", "ping curl"},
    {"Help", "man apropos whatis help"},
    {"Apps", "vim nano sh lua tcc make htop settings files terminal"},
};

int main(int argc, char **argv)
{
    util_init(argc, argv);
    bool color = util_color(1, NULL);
    printf("%sPureC OS%s — commands live in /bin, /usr/bin and /sbin.\n", color ? C_BOLD C_BLUE : "", color ? C_RESET : "");
    printf("Every command has a manual page: %sman <command>%s, search with %sman -k <word>%s.\n\n",
           color ? C_BOLD : "", color ? C_RESET : "", color ? C_BOLD : "", color ? C_RESET : "");
    for (size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); i++) {
        printf("  %s%-9s%s", color ? C_BOLD C_YELLOW : "", groups[i].title, color ? C_RESET : "");
        int col = 11, width = util_term_width();
        const char *p = groups[i].items;
        while (*p) {
            const char *sp = strchr(p, ' ');
            int len = sp ? (int)(sp - p) : (int)strlen(p);
            if (col + len + 1 > width - 2) {
                printf("\n%11s", "");
                col = 11;
            }
            printf(" %.*s", len, p);
            col += len + 1;
            p += len;
            while (*p == ' ') p++;
        }
        putchar('\n');
    }
    printf("\n  %sShell%s     cd pwd export unset alias type history; Up/Down history, Tab completion,\n"
           "            Ctrl+C interrupt, Ctrl+L clear, Ctrl+A/E line start/end, Ctrl+U/K/W delete\n",
           color ? C_BOLD C_YELLOW : "", color ? C_RESET : "");
    printf("  %sTerminal%s  Ctrl+Shift+T new tab, Ctrl+Shift+W close, Ctrl+Tab switch, Ctrl+= / Ctrl+- font size\n",
           color ? C_BOLD C_YELLOW : "", color ? C_RESET : "");
    return 0;
}
