#include "../../libc/include/purec.h"
#define EAT_CHUNK (1024ULL * 1024ULL)
#define EAT_DEFAULT_MB 1024
#define EAT_MAX_MB 4096
#define EAT_RETRIES 5
static uint64_t eat_parse_mb(const char *cmd){
    uint64_t v = 0;
    if(!cmd) return EAT_DEFAULT_MB;
    while(*cmd == ' ') cmd++;
    while(*cmd >= '0' && *cmd <= '9'){
        v = v * 10 + (uint64_t)(*cmd - '0');
        if(v > EAT_MAX_MB) return EAT_MAX_MB;
        cmd++;
        if(*cmd == ' ') break;
    }
    if(!v) return EAT_DEFAULT_MB;
    return v;
}
void _start(void){
    char cmd[256];
    uint64_t target_mb = EAT_DEFAULT_MB;
    if(pc_get_command_line(cmd, sizeof(cmd)) >= 0){
        uint32_t i = 0;
        while(cmd[i] && cmd[i] != ' ' && cmd[i] != '/') i++;
        while(cmd[i] == ' ') i++;
        if(cmd[i] >= '0' && cmd[i] <= '9') target_mb = eat_parse_mb(cmd + i);
    }
    struct memory_monitor_info mem;
    pc_write("eat: target ");
    pc_write_u64(target_mb);
    pc_write("MiB\n");
    if(pc_memory_info(&mem)){
        pc_write("eat: ram total ");
        pc_write_u64(mem.total_bytes / (1024ULL * 1024ULL));
        pc_write("MiB free ");
        pc_write_u64(mem.available_bytes / (1024ULL * 1024ULL));
        pc_write("MiB\n");
    }
    uint64_t eaten = 0;
    uint32_t denied = 0;
    uint64_t target = target_mb * 1024ULL * 1024ULL;
    for(;;){
        if(eaten >= target) break;
        void *p = pc_heap_grow(EAT_CHUNK);
        if(!p){
            denied++;
            pc_write("eat: refused at ");
            pc_write_u64(eaten / (1024ULL * 1024ULL));
            pc_write("MiB held (");
            pc_write_u64(denied);
            pc_write("/");
            pc_write_u64(EAT_RETRIES);
            pc_write(")\n");
            if(denied >= EAT_RETRIES) break;
            pc_sleep(1000);
            continue;
        }
        denied = 0;
        uint8_t *b = (uint8_t *)p;
        for(uint64_t i = 0; i < EAT_CHUNK; i += 4096) b[i] = (uint8_t)((eaten + i) & 0xFF);
        eaten += EAT_CHUNK;
        if((eaten / EAT_CHUNK) % 16 == 0){
            pc_write("eat: held ");
            pc_write_u64(eaten / (1024ULL * 1024ULL));
            pc_write("MiB\n");
        }
    }
    if(eaten >= target){
        pc_write("eat: target reached ");
        pc_write_u64(eaten / (1024ULL * 1024ULL));
        pc_write("MiB\n");
    } else {
        pc_write("eat: OOM sustained at ");
        pc_write_u64(eaten / (1024ULL * 1024ULL));
        pc_write("MiB held, kernel alive\n");
    }
    if(pc_memory_info(&mem)){
        pc_write("eat: ram free ");
        pc_write_u64(mem.available_bytes / (1024ULL * 1024ULL));
        pc_write("MiB\n");
    }
    pc_sleep(3000);
    pc_exit(0);
}
