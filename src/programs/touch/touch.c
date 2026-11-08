#include "../../libc/include/purec.h"
#include "../../libfs/include/purefs.h"
#include "../terminal/path.h"

#define TOUCH_PATH_CAPACITY 128
#define TOUCH_ARGUMENT_CAPACITY 512
#define TOUCH_TIME_NOW 0xFFFFFFFFu
#define TOUCH_ENOENT 2

static bool space(char c){
    return c == ' ' || c == '\t';
}

static const char *skip_spaces(const char *text){
    while(space(*text)) text++;
    return text;
}

static uint32_t next_word(const char **cursor, char *out, uint32_t capacity){
    const char *text = skip_spaces(*cursor);
    uint32_t len = 0;
    while(text[len] && !space(text[len])){
        if(len + 1 >= capacity) return 0xFFFFFFFFu;
        out[len] = text[len];
        len++;
    }
    out[len] = '\0';
    *cursor = text + len;
    return len;
}

static int touch_one(const char *target, bool no_create){
    int32_t status = pc_utimes(target, TOUCH_TIME_NOW, TOUCH_TIME_NOW);
    if(status >= 0) return 0;
    if(status != -TOUCH_ENOENT){
        pc_write("touch: cannot touch: ");
        pc_write(target);
        pc_write("\n");
        return 1;
    }
    if(no_create) return 0;

    char current[TOUCH_PATH_CAPACITY];
    if(pc_getenv("PWD", current, sizeof(current)) < 0){
        pc_copy(current, "/", sizeof(current));
    }
    char path[TOUCH_PATH_CAPACITY];
    if(!shell_path_normalize(current, target, path, sizeof(path))){
        pc_write("touch: invalid path: ");
        pc_write(target);
        pc_write("\n");
        return 1;
    }
    status = pf_create_file(path);
    if(status < 0){
        pc_write("touch: cannot create file: ");
        pc_write(target);
        pc_write("\n");
        return 1;
    }
    return 0;
}

int touch_main(void){
    char arguments[TOUCH_ARGUMENT_CAPACITY];
    if(pc_get_command_line(arguments, sizeof(arguments)) < 0){
        arguments[0] = '\0';
    }

    const char *cursor = skip_spaces(arguments);
    bool no_create = false;
    bool options_done = false;
    int files = 0;
    int failed = 0;
    char word[TOUCH_PATH_CAPACITY];

    for(;;){
        uint32_t len = next_word(&cursor, word, sizeof(word));
        if(len == 0xFFFFFFFFu){
            pc_write("touch: path too long\n");
            return 1;
        }
        if(len == 0) break;

        if(!options_done && word[0] == '-' && word[1]){
            if(pc_strcmp(word, "--") == 0){
                options_done = true;
                continue;
            }
            if(pc_strcmp(word, "--help") == 0){
                pc_write("usage: touch [-c] [-a] [-m] <file>...\n");
                pc_write("Update file times to now, creating missing files.\n");
                return 0;
            }
            if(pc_strcmp(word, "--no-create") == 0){
                no_create = true;
                continue;
            }
            bool valid = true;
            for(const char *flag = word + 1; *flag; flag++){
                if(*flag == 'c') no_create = true;
                else if(*flag == 'a' || *flag == 'm' || *flag == 'f' || *flag == 'h'){
                }
                else valid = false;
            }
            if(!valid){
                pc_write("touch: invalid option: ");
                pc_write(word);
                pc_write("\n");
                return 1;
            }
            continue;
        }

        files++;
        if(touch_one(word, no_create) != 0) failed = 1;
    }

    if(files == 0){
        pc_write("touch: file path required\n");
        return 1;
    }
    return failed;
}

void _start(void){
    pc_exit(touch_main());
}