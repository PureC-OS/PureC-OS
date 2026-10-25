#include "environment.h"
#include "../../libc/include/purec.h"

static bool variable_character(char character, bool first){
    return (character>='a' && character<='z')
        || (character>='A' && character<='Z') || character=='_'
        || (!first && character>='0' && character<='9');
}

static bool append_text(char *output, uint32_t *length, uint32_t capacity,
                        const char *text){
    for(uint32_t index=0;text[index];index++){
        if(*length+1>=capacity) return false;
        output[(*length)++]=text[index];
    }
    output[*length]='\0';
    return true;
}

bool shell_expand_environment(const char *input, char *output,
                              uint32_t capacity){
    if(!input || !output || !capacity) return false;
    uint32_t length=0;
    output[0]='\0';
    while(*input){
        if(*input!='$'){
            char text[2]={*input++,'\0'};
            if(!append_text(output,&length,capacity,text)) return false;
            continue;
        }
        input++;
        char name[PROCESS_ENVIRONMENT_NAME_LIMIT];
        uint32_t name_length=0;
        while(variable_character(*input,name_length==0)){
            if(name_length+1>=sizeof(name)) return false;
            name[name_length++]=*input++;
        }
        if(!name_length){
            if(!append_text(output,&length,capacity,"$")) return false;
            continue;
        }
        name[name_length]='\0';
        char value[PROCESS_ENVIRONMENT_VALUE_LIMIT];
        if(pc_getenv(name,value,sizeof(value))>=0
           && !append_text(output,&length,capacity,value)) return false;
    }
    return true;
}

void shell_print_environment(void){
    struct process_environment_variable variables[PROCESS_ENVIRONMENT_LIMIT];
    int32_t count=pc_listenv(variables,PROCESS_ENVIRONMENT_LIMIT);
    if(count<0){
        pc_write("env: cannot read environment\n");
        return;
    }
    if(count>PROCESS_ENVIRONMENT_LIMIT) count=PROCESS_ENVIRONMENT_LIMIT;
    for(int32_t index=0;index<count;index++){
        pc_write(variables[index].name);
        pc_write("=");
        pc_write(variables[index].value);
        pc_write("\n");
    }
}
