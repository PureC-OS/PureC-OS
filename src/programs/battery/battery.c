#include "../../libc/include/purec.h"

int battery_main(void){
    struct battery_info info = {0};
    if(pc_syscall(SYS_BATTERY_INFO, (uint64_t)(uintptr_t)&info, 0, 0) < 0){
        pc_write("battery: syscall failed\n");
        return 1;
    }
    if(!info.present){
        pc_write("battery: not present\n");
        return 0;
    }
    pc_write(info.name);
    pc_write(": ");
    if(info.percent == BATTERY_PERCENT_UNKNOWN){
        pc_write("unknown");
    } else {
        pc_write_u64(info.percent);
        pc_write("%");
    }
    pc_write(" ");
    pc_write(info.status_text);
    pc_write("\n");
    if(info.voltage_mv == 0){
        pc_write("voltage: unknown\n");
    } else {
        pc_write("voltage: ");
        pc_write_u64(info.voltage_mv);
        pc_write(" mV\n");
    }
    return 0;
}

void _start(void){
    pc_exit(battery_main());
}