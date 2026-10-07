#include "../../libc/include/purec.h"
#define EAT_CHUNK (1024ULL * 1024ULL)
void _start(void){
    struct memory_monitor_info mem;
    if(!pc_memory_info(&mem)){
        pc_write("eat: no memory info\n");
        pc_exit(1);
    }
    pc_write("eat: total ");
    pc_write_u64(mem.total_bytes);
    pc_write(" free ");
    pc_write_u64(mem.available_bytes);
    pc_write("\n");
    uint64_t eaten = 0;
    for(;;){
        void *p = pc_heap_grow(EAT_CHUNK);
        if(!p) break;
        uint8_t *b = (uint8_t *)p;
        for(uint64_t i = 0; i < EAT_CHUNK; i += 4096) b[i] = (uint8_t)(i & 0xFF);
        eaten += EAT_CHUNK;
        pc_write("eat: +1MiB held ");
        pc_write_u64(eaten / (1024ULL * 1024ULL));
        pc_write("MiB\n");
    }
    pc_write("eat: grow refused at ");
    pc_write_u64(eaten / (1024ULL * 1024ULL));
    pc_write("MiB held, kernel alive\n");
    if(pc_memory_info(&mem)){
        pc_write("eat: ram free ");
        pc_write_u64(mem.available_bytes);
        pc_write("\n");
    }
    pc_sleep(3000);
    pc_exit(0);
}
