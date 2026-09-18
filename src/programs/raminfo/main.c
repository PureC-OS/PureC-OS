#include "../../libc/include/purec.h"

int raminfo_main(void){
    struct memory_monitor_info memory={0};
    if(pc_syscall(SYS_MEMORY_INFO,(uint64_t)(uintptr_t)&memory,0,0)<0){
        pc_write("raminfo: syscall failed\n");
        return 1;
    }
    uint64_t total_mib=memory.total_bytes/(1024u*1024u);
    uint64_t used_mib=memory.used_bytes/(1024u*1024u);
    uint64_t avail_mib=memory.available_bytes/(1024u*1024u);
    uint32_t pct=memory.total_bytes
        ? (uint32_t)((memory.used_bytes*100)/memory.total_bytes) : 0;
    pc_write("RAM total: ");
    pc_write_u64(total_mib);
    pc_write(" MiB\nRAM used: ");
    pc_write_u64(used_mib);
    pc_write(" MiB (");
    pc_write_u64(pct);
    pc_write("%)\nRAM available: ");
    pc_write_u64(avail_mib);
    pc_write(" MiB\nFramebuffer: ");
    pc_write_u64(memory.framebuffer_bytes/(1024u*1024u));
    pc_write(" MiB\nStatus: ");
    if(!memory.total_bytes) pc_write("FAIL (no memory info)\n");
    else if(pct>=95) pc_write("FAIL (critical pressure)\n");
    else if(pct>=85) pc_write("WARN (high pressure)\n");
    else pc_write("PASS\n");
    return 0;
}

void _start(void){
    pc_exit(raminfo_main());
}
