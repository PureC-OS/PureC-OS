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
    char want[16];
    want[0]='\0';
    if(pc_strcmp(arguments,"classic")==0) pc_copy(want,"classic",sizeof(want));
    else if(pc_strcmp(arguments,"clean")==0) pc_copy(want,"clean",sizeof(want));
    else if(pc_strcmp(arguments,"bold")==0) pc_copy(want,"bold",sizeof(want));
    char buf[512];
    buf[0]='\0';
    int32_t fd=pc_file_open("/config/appear.ini");
    int32_t n=0;
    if(fd>=0){
        n=pc_file_read(fd,buf,sizeof(buf)-1);
        (void)pc_file_close(fd);
        if(n<0) n=0;
        buf[n]='\0';
    }
    if(!want[0]){
        char cur[16];
        cur[0]='\0';
        for(char *line=buf;*line;){
            char *end=line;
            while(*end && *end!='\n' && *end!='\r') end++;
            char save=*end;
            *end='\0';
            if(line[0]=='f' && line[1]=='o' && line[2]=='n' && line[3]=='t' && line[4]=='='){
                pc_copy(cur,line+5,sizeof(cur));
                break;
            }
            if(!save) break;
            line=end+1;
            while(*line=='\n' || *line=='\r') line++;
        }
        pc_write("Font: ");
        pc_write(cur[0] ? cur : "clean");
        pc_write("\nUse classic, clean or bold.\n");
        return 0;
    }
    {
        char out[512];
        char *w=out;
        uint32_t left=sizeof(out)-1;
        bool replaced=false;
        for(char *line=buf;*line;){
            char *end=line;
            while(*end && *end!='\n' && *end!='\r') end++;
            char save=*end;
            *end='\0';
            const char *src=line;
            char tmp[32];
            if(!replaced && line[0]=='f' && line[1]=='o' && line[2]=='n' && line[3]=='t' && line[4]=='='){
                tmp[0]='f'; tmp[1]='o'; tmp[2]='n'; tmp[3]='t'; tmp[4]='=';
                tmp[5]='\0';
                pc_copy(tmp+5,want,sizeof(tmp)-5);
                src=tmp;
                replaced=true;
            }
            uint32_t sl=pc_strlen(src);
            if(sl+1>left) break;
            for(uint32_t i=0;i<sl;i++) *w++=src[i];
            left-=sl;
            if(!save) break;
            if(left<2) break;
            *w++='\n';
            left--;
            line=end+1;
            while(*line=='\n' || *line=='\r') line++;
        }
        if(!replaced){
            uint32_t wl=pc_strlen(want);
            if(5+wl+2<=left){
                *w++='f'; *w++='o'; *w++='n'; *w++='t'; *w++='=';
                for(uint32_t i=0;i<wl;i++) *w++=want[i];
                *w++='\n';
            }
        }
        *w='\0';
        if(pc_file_write("/config/appear.ini",out,(uint32_t)(w-out))<0){
            pc_write("font: write failed\n");
            return 1;
        }
    }
    pc_write("Font set. Desktop and new windows pick it up.\n");
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

    if(pc_strcmp(name,"mouse")==0) return command_mouse();
    if(pc_strcmp(name,"debug")==0) return command_debug(arguments);
    return -1;
}
