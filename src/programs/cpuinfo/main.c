#include "../../libc/include/purec.h"

static uint32_t core_usage_percent(uint64_t total_a, uint64_t idle_a,
                                   uint64_t total_b, uint64_t idle_b){
    uint64_t elapsed=total_b>total_a ? total_b-total_a : 0;
    uint64_t idle_elapsed=idle_b>idle_a ? idle_b-idle_a : 0;
    if(!elapsed) return 0;
    if(idle_elapsed>elapsed) idle_elapsed=elapsed;
    return 100U-(uint32_t)((idle_elapsed*100)/elapsed);
}

int cpuinfo_main(void){
    struct cpu_monitor_info cpu={0};
    struct cpu_core_info before={0};
    struct cpu_core_info after={0};
    if(pc_syscall(SYS_CPU_INFO,(uint64_t)(uintptr_t)&cpu,0,0)<0){
        pc_write("cpuinfo: syscall failed\n");
        return 1;
    }
    pc_sleep(200);
    if(pc_syscall(SYS_CPU_INFO,(uint64_t)(uintptr_t)&cpu,0,0)<0){
        pc_write("cpuinfo: syscall failed\n");
        return 1;
    }
    pc_write("Processor: ");
    pc_write(cpu.name[0] ? cpu.name : "unknown");
    pc_write("\nLogical processors: ");
    pc_write_u64(cpu.logical_processors);
    pc_write("\nFrequency: ");
    pc_write_u64(cpu.frequency_hz/1000000);
    pc_write(" MHz\nUptime: ");
    pc_write_u64(cpu.uptime_ms/1000);
    pc_write("s\nTotal usage: ");
    pc_write_u64(cpu.usage_percent);
    pc_write("%\n");
    if(pc_cpu_core_info(&before)<0){
        pc_write("Per-core: unavailable\n");
        return 0;
    }
    pc_sleep(200);
    if(pc_cpu_core_info(&after)<0){
        pc_write("Per-core: unavailable\n");
        return 0;
    }
    uint32_t count=after.count;
    if(count>CPU_CORE_MAX_COUNT) count=CPU_CORE_MAX_COUNT;
    uint32_t online=0;
    for(uint32_t i=0;i<count;i++) if(after.cores[i].online) online++;
    pc_write("Online cores: ");
    pc_write_u64(online);
    pc_write(" / ");
    pc_write_u64(count);
    pc_write("\n");
    for(uint32_t i=0;i<count;i++){
        uint32_t b=i<before.count ? i : 0;
        uint32_t usage=core_usage_percent(
            before.cores[b].total_ticks,before.cores[b].idle_ticks,
            after.cores[i].total_ticks,after.cores[i].idle_ticks);
        pc_write("  cpu");
        pc_write_u64(after.cores[i].id);
        pc_write(": ");
        if(!after.cores[i].online) pc_write("offline\n");
        else {
            pc_write_u64(usage);
            pc_write("%\n");
        }
    }
    return 0;
}

void _start(void){
    pc_exit(cpuinfo_main());
}
