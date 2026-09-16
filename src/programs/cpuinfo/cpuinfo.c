#include "../../libc/include/purec.h"

int cpuinfo_main(void){
    struct cpu_monitor_info cpu = {0};
    if(pc_syscall(SYS_CPU_INFO, (uint64_t)(uintptr_t)&cpu, 0, 0) < 0){
        pc_write("cpuinfo: failed to get CPU information\n");
        return 1;
    }

    pc_write("Processor info:\n");
    pc_write("  Model: ");
    pc_write(cpu.name[0] ? cpu.name : "Unknown processor");
    pc_write("\n");
    
    pc_write("  Physical cores: ");
    pc_write_u64(cpu.physical_cores);
    pc_write("\n");
    pc_write("  Logical processors (threads): ");
    pc_write_u64(cpu.logical_processors);
    pc_write("\n");
    
    uint64_t frequency_ghz = cpu.frequency_hz / 1000000000ULL;
    uint64_t frequency_mhz = (cpu.frequency_hz % 1000000000ULL) / 1000000ULL;
    pc_write("  CPU frequency: ");
    pc_write_u64(frequency_ghz);
    pc_write(".");
    if(frequency_mhz < 100) pc_write("0");
    if(frequency_mhz < 10) pc_write("0");
    pc_write_u64(frequency_mhz);
    pc_write(" GHz (");
    pc_write_u64(cpu.frequency_hz / 1000000ULL);
    pc_write(" MHz)\n");
    
    pc_write("  Current CPU usage: ");
    pc_write_u64(cpu.usage_percent);
    pc_write("%\n");
    
    uint64_t uptime_seconds = cpu.uptime_ms / 1000ULL;
    uint64_t uptime_minutes = uptime_seconds / 60ULL;
    uint64_t uptime_hours = uptime_minutes / 60ULL;
    uptime_minutes %= 60ULL;
    uptime_seconds %= 60ULL;
    
    pc_write("  System uptime: ");
    pc_write_u64(uptime_hours);
    pc_write("h ");
    pc_write_u64(uptime_minutes);
    pc_write("m ");
    pc_write_u64(uptime_seconds);
    pc_write("s\n");

    return 0;
}

void _start(void){
    pc_exit(cpuinfo_main());
}