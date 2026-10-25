#include "../../libc/include/purec.h"
#include "../../libfs/include/purefs.h"
#include "../terminal/path.h"

#define LS_PATH_CAPACITY 128
#define LS_ARGUMENT_CAPACITY 256
#define LS_ENTRIES_CAPACITY 64

static bool space(char c){
    return c == ' ' || c == '\t';
}

static const char *skip_spaces(const char *text){
    while(space(*text)) text++;
    return text;
}

int ls_main(void){
    char arguments[LS_ARGUMENT_CAPACITY];
    if(pc_get_command_line(arguments, sizeof(arguments)) < 0){
        arguments[0] = '\0';
    }

    const char *cursor = skip_spaces(arguments);

    if(pc_strcmp(cursor, "--help") == 0 || pc_strcmp(cursor, "-h") == 0){
        pc_write("usage: ls [directory]\n");
        pc_write("List directory contents.\n");
        return 0;
    }

    char target[LS_PATH_CAPACITY];
    if(!*cursor){
        pc_copy(target, ".", sizeof(target));
    } else {
        uint32_t len = 0;
        while(cursor[len] && !space(cursor[len])){
            if(len + 1 >= sizeof(target)){
                pc_write("ls: path too long\n");
                return 1;
            }
            target[len] = cursor[len];
            len++;
        }
        target[len] = '\0';
    }

    char current[LS_PATH_CAPACITY];
    if(pc_getenv("PWD", current, sizeof(current)) < 0){
        pc_copy(current, "/", sizeof(current));
    }

    char path[LS_PATH_CAPACITY];
    if(!shell_path_normalize(current, target, path, sizeof(path))){
        pc_write("ls: invalid path\n");
        return 1;
    }

    struct pf_entry entries[LS_ENTRIES_CAPACITY];
    int32_t count = pf_list(path, entries, LS_ENTRIES_CAPACITY);
    if(count < 0){
        pc_write("ls: cannot list directory, error ");
        pc_write_i64(count);
        pc_write("\n");
        return 1;
    }

    if(!count){
        pc_write("(empty)\n");
        return 0;
    }

    for(int32_t index = 0; index < count; index++){
        pc_write(pf_is_dir(&entries[index]) ? "[DIR]  " : "[FILE] ");
        pc_write(entries[index].name);
        if(!pf_is_dir(&entries[index])){
            pc_write("  ");
            pc_write_u64(entries[index].size);
            pc_write(" bytes");
        }
        pc_write("\n");
    }

    return 0;
}

void _start(void){
    pc_exit(ls_main());
}

