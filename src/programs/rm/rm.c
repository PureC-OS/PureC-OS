#include "../../libc/include/purec.h"
#include "../../libfs/include/purefs.h"
#include "../terminal/path.h"

#define RM_PATH_CAPACITY 128
#define RM_ARGUMENT_CAPACITY 256
#define RM_LIST_CHUNK 16

static bool space(char c){
    return c == ' ' || c == '\t';
}

static const char *skip_spaces(const char *text){
    while(space(*text)) text++;
    return text;
}

static bool same(const char *a, const char *b){
    return pc_strcmp(a, b) == 0;
}

static bool is_root(const char *path){
    return path[0] == '/' && path[1] == '\0';
}

static bool last_is_dot(const char *raw){
    uint32_t len = 0;
    while(raw[len]) len++;
    while(len > 0 && raw[len - 1] == '/') len--;
    uint32_t start = len;
    while(start > 0 && raw[start - 1] != '/') start--;
    uint32_t n = len - start;
    if(n == 1 && raw[start] == '.') return true;
    if(n == 2 && raw[start] == '.' && raw[start + 1] == '.') return true;
    return false;
}

static bool is_dir_path(const char *path){
    struct file_stat_info info;
    if(pc_file_stat(path, &info) < 0) return false;
    return info.is_directory != 0;
}

static bool exists_path(const char *path){
    struct file_stat_info info;
    return pc_file_stat(path, &info) >= 0;
}

static void join_path(const char *parent, const char *name, char *out, uint32_t cap){
    uint32_t i = 0;
    while(parent[i] && i + 1 < cap){
        out[i] = parent[i];
        i++;
    }
    if(i + 1 < cap && !(i == 1 && out[0] == '/')){
        out[i] = '/';
        i++;
    }
    uint32_t j = 0;
    while(name[j] && i + 1 < cap){
        out[i] = name[j];
        i++;
        j++;
    }
    out[i] = '\0';
}

static int verbose_flag;

static void report_removed(const char *path){
    if(!verbose_flag) return;
    pc_write("removed '");
    pc_write(path);
    pc_write("'\n");
}

static int remove_recursive(const char *path, bool force);

static int remove_children(const char *path, bool force){
    for(;;){
        struct pf_entry_long entries[RM_LIST_CHUNK];
        int32_t count = pf_list_long(path, entries, RM_LIST_CHUNK);
        if(count < 0){
            if(!force){
                pc_write("rm: cannot list directory '");
                pc_write(path);
                pc_write("'\n");
            }
            return 1;
        }
        if(count == 0) return 0;
        for(int32_t i = 0; i < count; i++){
            if(same(entries[i].name, ".") || same(entries[i].name, "..")) continue;
            char child[RM_PATH_CAPACITY];
            join_path(path, entries[i].name, child, sizeof(child));
            if(child[0] == '\0'){
                pc_write("rm: path too long\n");
                return 1;
            }
            int failed = 0;
            if(entries[i].attributes & PF_ATTR_DIRECTORY){
                failed = remove_recursive(child, force);
            } else {
                if(pf_delete(child) < 0){
                    if(!force){
                        pc_write("rm: cannot remove '");
                        pc_write(child);
                        pc_write("'\n");
                    }
                    failed = 1;
                } else {
                    report_removed(child);
                }
            }
            if(failed) return 1;
        }
    }
}

static int remove_recursive(const char *path, bool force){
    int st = remove_children(path, force);
    if(st) return st;
    if(pf_delete(path) < 0){
        if(!force){
            pc_write("rm: cannot remove '");
            pc_write(path);
            pc_write("'\n");
        }
        return 1;
    }
    report_removed(path);
    return 0;
}

int rm_main(void){
    char arguments[RM_ARGUMENT_CAPACITY];
    if(pc_get_command_line(arguments, sizeof(arguments)) < 0){
        arguments[0] = '\0';
    }

    bool recursive = false;
    bool force = false;
    bool allow_root = false;
    bool dir_flag = false;
    bool operands_done = false;
    verbose_flag = false;

    const char *cursor = skip_spaces(arguments);
    char operands[RM_ARGUMENT_CAPACITY];
    uint32_t olen = 0;
    operands[0] = '\0';

    while(*cursor){
        char token[RM_PATH_CAPACITY];
        uint32_t len = 0;
        while(cursor[len] && !space(cursor[len])){
            if(len + 1 >= sizeof(token)){
                pc_write("rm: argument too long\n");
                return 1;
            }
            token[len] = cursor[len];
            len++;
        }
        token[len] = '\0';
        cursor += len;
        cursor = skip_spaces(cursor);

        if(!operands_done && token[0] == '-' && token[1] != '\0' && !same(token, "-")){
            if(same(token, "--")){
                operands_done = true;
                continue;
            }
            if(token[1] == '-'){
                if(same(token, "--recursive")){ recursive = true; continue; }
                if(same(token, "--force")){ force = true; continue; }
                if(same(token, "--verbose")){ verbose_flag = true; continue; }
                if(same(token, "--dir")){ dir_flag = true; continue; }
                if(same(token, "--preserve-root")){ allow_root = false; continue; }
                if(same(token, "--no-preserve-root")){ allow_root = true; continue; }
                if(same(token, "--help")){
                    pc_write("usage: rm [-rRfvd] [--no-preserve-root] <path>...\n");
                    pc_write("Remove files or directories. Removing '/' is refused\n");
                    pc_write("unless --no-preserve-root is given with -r.\n");
                    return 0;
                }
                pc_write("rm: invalid option -- '");
                pc_write(token);
                pc_write("'\n");
                return 1;
            }
            for(uint32_t i = 1; token[i]; i++){
                if(token[i] == 'r' || token[i] == 'R') recursive = true;
                else if(token[i] == 'f') force = true;
                else if(token[i] == 'v') verbose_flag = true;
                else if(token[i] == 'd') dir_flag = true;
                else if(token[i] == 'h'){
                    pc_write("usage: rm [-rRfvd] [--no-preserve-root] <path>...\n");
                    return 0;
                } else {
                    pc_write("rm: invalid option -- '");
                    char bad[2] = { (char)token[i], '\0' };
                    pc_write(bad);
                    pc_write("'\n");
                    return 1;
                }
            }
            continue;
        }

        uint32_t tlen = 0;
        while(token[tlen]) tlen++;
        if(olen + tlen + 2 >= sizeof(operands)){
            pc_write("rm: too many operands\n");
            return 1;
        }
        if(olen) operands[olen++] = ' ';
        for(uint32_t i = 0; i <= tlen; i++) operands[olen++] = token[i];
    }

    if(!operands[0]){
        pc_write("rm: missing operand\n");
        return 1;
    }

    char current[RM_PATH_CAPACITY];
    if(pc_getenv("PWD", current, sizeof(current)) < 0){
        pc_copy(current, "/", sizeof(current));
    }

    int result = 0;
    const char *op = operands;
    while(*op){
        char raw[RM_PATH_CAPACITY];
        uint32_t len = 0;
        while(op[len] && !space(op[len])){
            if(len + 1 >= sizeof(raw)){
                pc_write("rm: path too long\n");
                result = 1;
                break;
            }
            raw[len] = op[len];
            len++;
        }
        raw[len] = '\0';
        op += len;
        op = skip_spaces(op);

        char path[RM_PATH_CAPACITY];
        if(!shell_path_normalize(current, raw, path, sizeof(path))){
            pc_write("rm: invalid path\n");
            result = 1;
            continue;
        }

        if(is_root(path)){
            if(recursive && allow_root){
            } else if(recursive){
                pc_write("rm: it is dangerous to operate recursively on '/'\n");
                pc_write("rm: use --no-preserve-root to override this failsafe\n");
                result = 1;
                continue;
            } else {
                pc_write("rm: cannot remove '/': Is a directory\n");
                result = 1;
                continue;
            }
        }

        if(last_is_dot(raw)){
            pc_write("rm: refusing to remove '.' or '..' directory: skipping '");
            pc_write(raw);
            pc_write("'\n");
            result = 1;
            continue;
        }

        if(!exists_path(path)){
            if(!force){
                pc_write("rm: cannot remove '");
                pc_write(raw);
                pc_write("': No such file or directory\n");
                result = 1;
            }
            continue;
        }

        if(is_dir_path(path)){
            if(recursive){
                if(remove_recursive(path, force)) result = 1;
            } else if(dir_flag){
                if(pf_delete(path) < 0){
                    if(!force){
                        pc_write("rm: cannot remove '");
                        pc_write(raw);
                        pc_write("': Directory not empty\n");
                    }
                    result = 1;
                } else {
                    report_removed(raw);
                }
            } else {
                if(!force){
                    pc_write("rm: cannot remove '");
                    pc_write(raw);
                    pc_write("': Is a directory\n");
                }
                result = 1;
            }
            continue;
        }

        if(pf_delete(path) < 0){
            if(!force){
                pc_write("rm: cannot remove '");
                pc_write(raw);
                pc_write("'\n");
            }
            result = 1;
        } else {
            report_removed(raw);
        }
    }

    return result;
}

void _start(void){
    pc_exit(rm_main());
}
