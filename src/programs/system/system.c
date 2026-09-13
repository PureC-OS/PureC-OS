#include "system.h"
#include "../../libc/include/purec.h"

#define SYSTEM_DEVICE_CAPACITY 20

static int run_program(const char *path, const char *arguments){
    int32_t pid=pc_exec_with_args(path,arguments);
    if(pid<0){
        pc_write("cannot execute ");
        pc_write(path);
        pc_write("\n");
        return 1;
    }
    int32_t status=0;
    return pc_wait(pid,&status,false)<0 ? 1 : status;
}

static int command_disks(void){
    struct storage_device_info devices[SYSTEM_DEVICE_CAPACITY];
    int32_t count=pc_list_disks(devices,SYSTEM_DEVICE_CAPACITY);
    if(count<0){
        pc_write("disks: enumeration failed\n");
        return 1;
    }
    if(!count){
        pc_write("No block devices detected.\n");
        return 0;
    }
    for(int32_t index=0;index<count;index++){
        pc_write(devices[index].name);
        pc_write("  ");
        pc_write_u64(devices[index].sector_count*devices[index].sector_size
                     /(1024u*1024u));
        pc_write(" MiB  ");
        pc_write(devices[index].model[0] ? devices[index].model : "disk");
        pc_write(devices[index].writable ? "  rw\n" : "  ro\n");
    }
    return 0;
}

static int command_usbscan(void){
    struct usb_scan_status status={0};
    int64_t count=pc_syscall(SYS_USB_RESCAN,(uint64_t)(uintptr_t)&status,0,0);
    if(count<0){
        pc_write("usbscan: scan failed\n");
        return 1;
    }
    pc_write("USB disks: ");
    pc_write_i64(count);
    pc_write("\nxHCI controllers: ");
    pc_write_u64(status.xhci_controllers);
    pc_write(" connected ports: ");
    pc_write_u64(status.xhci_connected_ports);
    pc_write(" addressed devices: ");
    pc_write_u64(status.xhci_addressed_devices);
    pc_write("\nEHCI connected ports: ");
    pc_write_u64(status.ehci_connected_ports);
    pc_write("\n");
    return 0;
}

static int command_systeminfo(void){
    struct cpu_monitor_info cpu={0};
    struct memory_monitor_info memory={0};
    struct pc_display_info display={0};
    (void)pc_syscall(SYS_CPU_INFO,(uint64_t)(uintptr_t)&cpu,0,0);
    (void)pc_syscall(SYS_MEMORY_INFO,(uint64_t)(uintptr_t)&memory,0,0);
    (void)pc_display_get_info(&display);
    pc_write("Processor: ");
    pc_write(cpu.name[0] ? cpu.name : "unknown");
    pc_write("\nLogical processors: ");
    pc_write_u64(cpu.logical_processors);
    pc_write("\nAvailable RAM: ");
    pc_write_u64(memory.available_bytes/(1024u*1024u));
    pc_write(" MiB\nDisplay: ");
    pc_write_u64(display.width);
    pc_write("x");
    pc_write_u64(display.height);
    pc_write("\n");
    return 0;
}

static int command_htop(void){
    struct cpu_monitor_info cpu={0};
    struct memory_monitor_info memory={0};
    if(pc_syscall(SYS_CPU_INFO,(uint64_t)(uintptr_t)&cpu,0,0)<0
       || pc_syscall(SYS_MEMORY_INFO,(uint64_t)(uintptr_t)&memory,0,0)<0)
        return 1;
    pc_write("CPU: ");
    pc_write_u64(cpu.usage_percent);
    pc_write("%  uptime: ");
    pc_write_u64(cpu.uptime_ms/1000);
    pc_write("s\nRAM used: ");
    pc_write_u64(memory.used_bytes/(1024u*1024u));
    pc_write(" MiB / ");
    pc_write_u64(memory.total_bytes/(1024u*1024u));
    pc_write(" MiB\n");
    return 0;
}

static int command_font(const char *arguments){
    if(pc_strcmp(arguments,"classic")==0)
        return pc_syscall(SYS_SET_FONT_FACE,0,0,0)<0;
    if(pc_strcmp(arguments,"clean")==0)
        return pc_syscall(SYS_SET_FONT_FACE,1,0,0)<0;
    if(pc_strcmp(arguments,"bold")==0)
        return pc_syscall(SYS_SET_FONT_FACE,2,0,0)<0;
    int64_t face=pc_syscall(SYS_GET_FONT_FACE,0,0,0);
    pc_write("Font: ");
    pc_write(face==1 ? "clean\n" : (face==2 ? "bold\n" : "classic\n"));
    pc_write("Use classic, clean or bold.\n");
    return 0;
}

static int command_mouse(void){
    struct mouse_state state;
    if(!pc_mouse_get(&state)) return 1;
    pc_write("mouse: x=");
    pc_write_i64(state.x);
    pc_write(" y=");
    pc_write_i64(state.y);
    pc_write(" buttons=");
    pc_write_u64(state.buttons);
    pc_write("\n");
    return 0;
}

static int command_debug(const char *arguments){
    bool enabled=pc_syscall(SYS_MOUSE_DEBUG_GET,0,0,0)>0;
    if(pc_strcmp(arguments,"on")==0) enabled=true;
    else if(pc_strcmp(arguments,"off")==0) enabled=false;
    else if(arguments[0]){
        pc_write("debug: use on or off\n");
        return 1;
    } else enabled=!enabled;
    if(pc_syscall(SYS_MOUSE_DEBUG_SET,enabled ? 1 : 0,0,0)<0) return 1;
    pc_write("Mouse debug panel: ");
    pc_write(enabled ? "on\n" : "off\n");
    return 0;
}

static int command_battery(void){
    struct battery_info info={0};
    if(pc_syscall(SYS_BATTERY_INFO,(uint64_t)(uintptr_t)&info,0,0)<0) return 1;
    if(!info.present){
        pc_write("battery: not present\n");
        return 0;
    }
    pc_write(info.name);
    pc_write(": ");
    pc_write_u64(info.percent);
    pc_write("% ");
    pc_write(info.status_text);
    pc_write("\n");
    return 0;
}

int system_platform_command(const char *name, const char *arguments){
    if(pc_strcmp(name,"disks")==0) return command_disks();
    if(pc_strcmp(name,"usbscan")==0) return command_usbscan();
    if(pc_strcmp(name,"install")==0 || pc_strcmp(name,"setup")==0
       || pc_strcmp(name,"update")==0 || pc_strcmp(name,"mkfs.fat32")==0)
        return run_program("/bin/installer",arguments);
    if(pc_strcmp(name,"uname")==0){
        pc_write("PureC OS 0.1.0 x86_64\n");
        return 0;
    }
    if(pc_strcmp(name,"about")==0){
        pc_write("PureC userspace 0.3.0, standalone system programs\n");
        return 0;
    }
    if(pc_strcmp(name,"systeminfo")==0) return command_systeminfo();
    if(pc_strcmp(name,"htop")==0) return command_htop();
    if(pc_strcmp(name,"font")==0) return command_font(arguments);
    if(pc_strcmp(name,"snake")==0) return run_program("/bin/snake",arguments);
    if(pc_strcmp(name,"tetris")==0) return run_program("/bin/tetris",arguments);
    if(pc_strcmp(name,"mouse")==0) return command_mouse();
    if(pc_strcmp(name,"debug")==0) return command_debug(arguments);
    if(pc_strcmp(name,"reboot")==0)
        return pc_syscall(SYS_REBOOT,0,0,0)<0;
    if(pc_strcmp(name,"poweroff")==0 || pc_strcmp(name,"shutdown")==0
       || pc_strcmp(name,"halt")==0)
        return pc_syscall(SYS_SHUTDOWN,0,0,0)<0;
    if(pc_strcmp(name,"battery")==0) return command_battery();
    return -1;
}
