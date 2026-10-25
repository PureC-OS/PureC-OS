#include "path.h"
#include "../../libc/include/purec.h"

bool files_path_join(char *result, uint32_t capacity,
                     const char *directory, const char *name){
    if(!result || !capacity || !directory || !name || !name[0]) return false;
    uint32_t directory_length=pc_strlen(directory);
    uint32_t name_length=pc_strlen(name);
    bool root=directory_length==1 && directory[0]=='/';
    uint32_t required=directory_length+(root ? 0 : 1)+name_length+1;
    if(required>capacity) return false;
    pc_copy(result,directory,capacity);
    uint32_t position=directory_length;
    if(!root) result[position++]='/';
    for(uint32_t index=0;index<name_length;index++)
        result[position++]=name[index];
    result[position]='\0';
    return true;
}

bool files_path_parent(char *path, uint32_t capacity){
    if(!path || capacity<2 || path[0]!='/' || path[1]=='\0') return false;
    uint32_t length=pc_strlen(path);
    while(length>1 && path[length-1]=='/') length--;
    while(length>1 && path[length-1]!='/') length--;
    if(length<=1){
        path[0]='/';
        path[1]='\0';
    } else {
        path[length-1]='\0';
    }
    return true;
}
