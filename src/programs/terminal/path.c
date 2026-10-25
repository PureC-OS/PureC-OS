#include "path.h"
#include "../../libc/include/purec.h"

static bool append_component(char *output, uint32_t *length,
                             uint32_t capacity, const char *component,
                             uint32_t component_length){
    uint32_t required=*length+component_length+(*length>1 ? 1 : 0);
    if(required>=capacity) return false;
    if(*length>1) output[(*length)++]='/';
    for(uint32_t index=0;index<component_length;index++)
        output[(*length)++]=component[index];
    output[*length]='\0';
    return true;
}

static void remove_component(char *output, uint32_t *length){
    if(*length<=1) return;
    while(*length>1 && output[*length-1]!='/') (*length)--;
    if(*length>1) (*length)--;
    output[*length]='\0';
}

bool shell_path_normalize(const char *base, const char *path,
                          char *output, uint32_t capacity){
    if(!base || !path || !output || capacity<2) return false;
    uint32_t length=0;
    if(path[0]=='/'){
        output[length++]='/';
        output[length]='\0';
    } else {
        length=pc_strlen(base);
        if(length>=capacity || base[0]!='/') return false;
        pc_copy(output,base,capacity);
    }
    while(*path){
        while(*path=='/') path++;
        if(!*path) break;
        const char *component=path;
        uint32_t component_length=0;
        while(path[component_length] && path[component_length]!='/')
            component_length++;
        path+=component_length;
        if(component_length==1 && component[0]=='.') continue;
        if(component_length==2 && component[0]=='.' && component[1]=='.'){
            remove_component(output,&length);
            continue;
        }
        if(!append_component(output,&length,capacity,component,
                             component_length)) return false;
    }
    return true;
}
