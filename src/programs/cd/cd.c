#include "../../libc/include/purec.h"
#include "../../libfs/include/purefs.h"
#include "../terminal/path.h"

#define CD_PATH_CAPACITY 128
#define CD_ARGUMENT_CAPACITY 256

static bool space(char c){
    return c == ' ' || c == '\t';
}

static const char *skip_spaces(const char *text){
    while(space(*text)) text++;
    return text;
}

int cd_main(void){
    char arguments[CD_ARGUMENT_CAPACITY];
    if(pc_get_command_line(arguments, sizeof(arguments)) < 0){
        arguments[0] = '\0';
    }

    const char *cursor = skip_spaces(arguments);

    // Help flag
    if(pc_strcmp(cursor, "--help") == 0 || pc_strcmp(cursor, "-h") == 0){
        pc_write("usage: cd [directory]\n");
        pc_write("Change the current working directory.\n");
        return 0;
    }

    char target[CD_PATH_CAPACITY];
    if(!*cursor){
        if(pc_getenv("HOME", target, sizeof(target)) < 0 || !target[0]){
            pc_copy(target, "/", sizeof(target));
        }
    } else {
        uint32_t len = 0;
        while(cursor[len] && !space(cursor[len])){
            if(len + 1 >= sizeof(target)){
                pc_write("cd: path too long\n");
                return 1;
            }
            target[len] = cursor[len];
            len++;
        }
        target[len] = '\0';
    }

    char current[CD_PATH_CAPACITY];
    if(pc_getenv("PWD", current, sizeof(current)) < 0){
        pc_copy(current, "/", sizeof(current));
    }

    char normalized[CD_PATH_CAPACITY];
    if(!shell_path_normalize(current, target, normalized, sizeof(normalized))){
        pc_write("cd: invalid path: ");
        pc_write(target);
        pc_write("\n");
        return 1;
    }

    // Root directory "/" is always valid
    if(pc_strcmp(normalized, "/") != 0){
        struct pf_entry entries[1];
        int32_t count = pf_list(normalized, entries, 1);
        if(count < 0){
            pc_write("cd: no such directory: ");
            pc_write(target);
            pc_write("\n");
            return 1;
        }
    }

    pc_setenv("PWD", normalized);
    return 0;
}

void _start(void){
    pc_exit(cd_main());
}

