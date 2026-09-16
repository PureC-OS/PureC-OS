#include "../../libc/include/purec.h"
#include "../../libfs/include/purefs.h"
#include "../terminal/path.h"

#define CAT_PATH_CAPACITY 128
#define CAT_ARGUMENT_CAPACITY 256
#define CAT_BUFFER_CAPACITY 256

static bool space(char c){
    return c == ' ' || c == '\t';
}

static const char *skip_spaces(const char *text){
    while(space(*text)) text++;
    return text;
}

int cat_main(void){
    char arguments[CAT_ARGUMENT_CAPACITY];
    if(pc_get_command_line(arguments, sizeof(arguments)) < 0){
        arguments[0] = '\0';
    }

    const char *cursor = skip_spaces(arguments);
    if(pc_strcmp(cursor, "--help") == 0 || pc_strcmp(cursor, "-h") == 0){
        pc_write("usage: cat <file>\n");
        pc_write("Concatenate and print file contents.\n");
        return 0;
    }

    if(!*cursor){
        pc_write("cat: file path required\n");
        return 1;
    }

    char target[CAT_PATH_CAPACITY];
    uint32_t len = 0;
    while(cursor[len] && !space(cursor[len])){
        if(len + 1 >= sizeof(target)){
            pc_write("cat: path too long\n");
            return 1;
        }
        target[len] = cursor[len];
        len++;
    }
    target[len] = '\0';

    char current[CAT_PATH_CAPACITY];
    if(pc_getenv("PWD", current, sizeof(current)) < 0){
        pc_copy(current, "/", sizeof(current));
    }

    char path[CAT_PATH_CAPACITY];
    if(!shell_path_normalize(current, target, path, sizeof(path))){
        pc_write("cat: invalid path\n");
        return 1;
    }

    int32_t fd = pf_open(path);
    if(fd < 0){
        pc_write("cat: cannot open file: ");
        pc_write(target);
        pc_write("\n");
        return 1;
    }

    bool wrote = false;
    char last = '\0';
    for(;;){
        char buffer[CAT_BUFFER_CAPACITY];
        int32_t count = pf_read(fd, buffer, sizeof(buffer));
        if(count < 0){
            (void)pf_close(fd);
            pc_write("cat: read failed\n");
            return 1;
        }
        if(!count) break;
        for(int32_t i = 0; i < count; i++){
            char ch[2] = {buffer[i], '\0'};
            pc_write(ch);
            last = buffer[i];
        }
        wrote = true;
    }
    (void)pf_close(fd);
    if(wrote && last != '\n') pc_write("\n");
    return 0;
}

void _start(void){
    pc_exit(cat_main());
}

