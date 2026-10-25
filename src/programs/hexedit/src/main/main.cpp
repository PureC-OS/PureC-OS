#include "hexedit/editor/editor.hpp"

extern "C" {
#include "../../../../libc/include/purec.h"
}

extern "C" void _start(void)
{
    char args[128];
    int32_t n = pc_get_command_line(args, sizeof(args));
    const char* target = nullptr;
    char path[128] = "";

    if (n > 0)
    {
        const char* p = args;

        while (*p == ' ' || *p == '\t')
        {
            ++p;
        }

        if (*p)
        {
            uint32_t len = 0;
            while (p[len] && p[len] != ' ' && p[len] != '\t')
            {
                ++len;
            }

            const char* rem = p + len;
            while (*rem == ' ' || *rem == '\t')
            {
                ++rem;
            }

            if (*rem)
            {
                pc_write("hexedit: exactly one path allowed\n");
                pc_exit(2);
            }

            if (p[0] != '/')
            {
                char pwd[128];
                if (pc_getenv("PWD", pwd, sizeof(pwd)) < 0)
                {
                    pc_copy(pwd, "/", sizeof(pwd));
                }

                uint32_t dl = pc_strlen(pwd);
                uint32_t need = dl + (dl > 1 ? 1 : 0) + len + 1;

                if (need > sizeof(path))
                {
                    pc_write("hexedit: path too long\n");
                    pc_exit(2);
                }

                pc_copy(path, pwd, sizeof(path));

                if (dl > 1)
                {
                    path[dl++] = '/';
                    path[dl] = '\0';
                }

                for (uint32_t i = 0; i < len; ++i)
                {
                    path[dl++] = p[i];
                }
                path[dl] = '\0';
            }
            else
            {
                if (len + 1 > sizeof(path))
                {
                    pc_write("hexedit: path too long\n");
                    pc_exit(2);
                }
                for (uint32_t i = 0; i < len; ++i)
                {
                    path[i] = p[i];
                }
                path[len] = '\0';
            }

            target = path;
        }
    }

    int rc = hexedit_run(target);
    pc_exit(rc);
}
