#include "../../libc/include/purec.h"
#include "../../libfs/include/purefs.h"
#include "../terminal/path.h"

#define TOUCH_PATH_CAPACITY 128
#define TOUCH_ARGUMENT_CAPACITY 256

static bool space(char c){
    return c == ' ' || c == '\t';
}

static const char *skip_spaces(const char *text){
    while(space(*text)) text++;
    return text;
}

int touch_main(void){
    char arguments[TOUCH_ARGUMENT_CAPACITY];
    if(pc_get_command_line(arguments, sizeof(arguments)) < 0){
        arguments[0] = '\0';
    }

    const char *cursor = skip_spaces(arguments);
    if(pc_strcmp(cursor, "--help") == 0 || pc_strcmp(cursor, "-h") == 0){
        pc_write("usage: touch <file>\n");
        pc_write("Create an empty file.\n");
        return 0;
    }

    if(!*cursor){
        pc_write("touch: file path required\n");
        return 1;
    }

    char target[TOUCH_PATH_CAPACITY];
    uint32_t len = 0;
    while(cursor[len] && !space(cursor[len])){
        if(len + 1 >= sizeof(target)){
            pc_write("touch: path too long\n");
            return 1;
        }
        target[len] = cursor[len];
        len++;
    }
    target[len] = '\0';

    char current[TOUCH_PATH_CAPACITY];
    if(pc_getenv("PWD", current, sizeof(current)) < 0){
        pc_copy(current, "/", sizeof(current));
    }

    char path[TOUCH_PATH_CAPACITY];
    if(!shell_path_normalize(current, target, path, sizeof(path))){
        pc_write("touch: invalid path\n");
        return 1;
    }

    int32_t status = pf_create_file(path);
    if(status < 0){
        pc_write("touch: cannot create file: ");
        pc_write(target);
        pc_write("\n");
        return 1;
    }

    return 0;
}

void _start(void){
    pc_exit(touch_main());
}

