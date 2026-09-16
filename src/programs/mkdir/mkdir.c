#include "../../libc/include/purec.h"
#include "../../libfs/include/purefs.h"
#include "../terminal/path.h"

#define MKDIR_PATH_CAPACITY 128
#define MKDIR_ARGUMENT_CAPACITY 256

static bool space(char c){
    return c == ' ' || c == '\t';
}

static const char *skip_spaces(const char *text){
    while(space(*text)) text++;
    return text;
}

int mkdir_main(void){
    char arguments[MKDIR_ARGUMENT_CAPACITY];
    if(pc_get_command_line(arguments, sizeof(arguments)) < 0){
        arguments[0] = '\0';
    }

    const char *cursor = skip_spaces(arguments);
    if(pc_strcmp(cursor, "--help") == 0 || pc_strcmp(cursor, "-h") == 0){
        pc_write("usage: mkdir <directory>\n");
        pc_write("Create a new directory.\n");
        return 0;
    }

    if(!*cursor){
        pc_write("mkdir: directory path required\n");
        return 1;
    }

    char target[MKDIR_PATH_CAPACITY];
    uint32_t len = 0;
    while(cursor[len] && !space(cursor[len])){
        if(len + 1 >= sizeof(target)){
            pc_write("mkdir: path too long\n");
            return 1;
        }
        target[len] = cursor[len];
        len++;
    }
    target[len] = '\0';

    char current[MKDIR_PATH_CAPACITY];
    if(pc_getenv("PWD", current, sizeof(current)) < 0){
        pc_copy(current, "/", sizeof(current));
    }

    char path[MKDIR_PATH_CAPACITY];
    if(!shell_path_normalize(current, target, path, sizeof(path))){
        pc_write("mkdir: invalid path\n");
        return 1;
    }

    int32_t status = pf_create_dir(path);
    if(status < 0){
        pc_write("mkdir: cannot create directory: ");
        pc_write(target);
        pc_write("\n");
        return 1;
    }

    return 0;
}

void _start(void){
    pc_exit(mkdir_main());
}

