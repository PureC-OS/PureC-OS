#include "../../libc/include/purec.h"
#define EAT_CHUNK (1024ULL * 1024ULL)
#define EAT_DEFAULT_MB 1024
#define EAT_MAX_MB 4096
#define EAT_MAX_BLOBS 4096
#define EAT_LIST_CHUNK 64
static void *eat_blobs[EAT_MAX_BLOBS];
static uint32_t eat_blob_count;
static int32_t eat_my_parent(void){
    int32_t me = pc_getpid();
    struct process_monitor_info page[EAT_LIST_CHUNK];
    uint32_t offset = 0;
    for(;;){
        int32_t total = pc_process_list_page(page, EAT_LIST_CHUNK, offset);
        if(total <= 0) return -1;
        int32_t got = total - (int32_t)offset;
        if(got > EAT_LIST_CHUNK) got = EAT_LIST_CHUNK;
        if(got <= 0) return -1;
        for(int32_t i = 0; i < got; i++){
            if(page[i].pid == (uint32_t)me) return (int32_t)page[i].parent_pid;
        }
        offset += (uint32_t)got;
        if(offset >= (uint32_t)total) return -1;
    }
}
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
static void eat_touch_all(uint8_t pat){
    for(uint32_t b = 0; b < eat_blob_count; b++){
        uint8_t *p = (uint8_t *)eat_blobs[b];
        for(uint64_t i = 0; i < EAT_CHUNK; i += 4096) p[i] = (uint8_t)(pat + (i & 0xFF));
    }
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
    pc_write("MiB, hold until terminal closes\n");
    uint64_t target = target_mb * 1024ULL * 1024ULL;
    uint64_t eaten = 0;
    for(;;){
        if(eaten >= target) break;
        if(eat_blob_count >= EAT_MAX_BLOBS) break;
        void *p = pc_heap_grow(EAT_CHUNK);
        if(!p) break;
        eat_blobs[eat_blob_count++] = p;
        uint8_t *b = (uint8_t *)p;
        for(uint64_t i = 0; i < EAT_CHUNK; i += 4096) b[i] = (uint8_t)((eaten + i) & 0xFF);
        eaten += EAT_CHUNK;
        if((eaten / EAT_CHUNK) % 16 == 0){
            pc_write("eat: held ");
            pc_write_u64(eaten / (1024ULL * 1024ULL));
            pc_write("MiB\n");
        }
    }
    pc_write("eat: holding ");
    pc_write_u64(eaten / (1024ULL * 1024ULL));
    pc_write("MiB, close terminal to stop\n");
    int32_t home = eat_my_parent();
    uint8_t pat = 0;
    uint32_t tick = 0;
    for(;;){
        pat++;
        eat_touch_all(pat);
        if(eat_blob_count < EAT_MAX_BLOBS){
            void *p = pc_heap_grow(EAT_CHUNK);
            if(p){
                eat_blobs[eat_blob_count++] = p;
                eaten += EAT_CHUNK;
                pc_write("eat: regrew +1MiB held ");
                pc_write_u64(eaten / (1024ULL * 1024ULL));
                pc_write("MiB\n");
            }
        }
        tick++;
        if(tick % 30 == 0){
            pc_write("eat: still holding ");
            pc_write_u64(eaten / (1024ULL * 1024ULL));
            pc_write("MiB\n");
        }
        if(home > 0 && eat_my_parent() != home){
            pc_write("eat: terminal closed, exiting\n");
            break;
        }
        pc_sleep(1000);
    }
    pc_exit(0);
}
