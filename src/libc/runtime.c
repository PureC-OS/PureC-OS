#include "include/purec.h"
#include "../kernel/syscall/syscall.h"

int64_t pc_syscall(uint64_t number, uint64_t argument1,
                   uint64_t argument2, uint64_t argument3){
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number),"b"(argument1),"c"(argument2),"d"(argument3)
        : "r10","r8","memory"
    );
    return result;
}

static int64_t syscall5(uint64_t number, uint64_t argument1,
                        uint64_t argument2, uint64_t argument3,
                        uint64_t argument4, uint64_t argument5){
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number),"b"(argument1),"c"(argument2),"d"(argument3),
          "S"(argument4),"D"(argument5)
        : "r10","r8","memory"
    );
    return result;
}

uint32_t pc_strlen(const char *text){
    uint32_t length=0;
    while(text[length]) length++;
    return length;
}

int pc_strcmp(const char *left, const char *right){
    while(*left && *left==*right){ left++; right++; }
    return (uint8_t)*left-(uint8_t)*right;
}

void pc_copy(char *destination, const char *source, uint32_t capacity){
    if(!capacity) return;
    uint32_t index=0;
    while(source[index] && index+1<capacity){
        destination[index]=source[index];
        index++;
    }
    destination[index]='\0';
}

void pc_write(const char *text){
    (void)pc_syscall(SYS_WRITE,(uint64_t)(uintptr_t)text,pc_strlen(text),1);
}

void pc_write_u64(uint64_t value){
    char buffer[21];
    uint32_t index=sizeof(buffer);
    buffer[--index]='\0';
    do {
        buffer[--index]=(char)('0'+value%10);
        value/=10;
    } while(value);
    pc_write(&buffer[index]);
}

void pc_write_i64(int64_t value){
    if(value<0){
        pc_write("-");
        pc_write_u64((uint64_t)(-(value+1))+1);
        return;
    }
    pc_write_u64((uint64_t)value);
}

void pc_read_line(const char *prompt, char *buffer, uint32_t capacity){
    pc_write(prompt);
    if(!capacity) return;
    uint32_t length=0;
    for(;;){
        char character=(char)pc_syscall(SYS_GETCHAR,0,0,0);
        if(character=='\r' || character=='\n'){
            pc_write("\n");
            break;
        }
        if((character=='\b' || character==127) && length){
            length--;
            pc_write("\b");
            continue;
        }
        if(character<' ' || character>'~') continue;
        if(length+1<capacity){
            char echo[2]={character,'\0'};
            buffer[length++]=character;
            pc_write(echo);
        }
    }
    buffer[length]='\0';
}

void pc_sleep(uint32_t milliseconds){
    (void)pc_syscall(SYS_SLEEP,milliseconds,0,0);
}

int32_t pc_getpid(void){ return (int32_t)pc_syscall(SYS_GETPID,0,0,0); }

int32_t pc_exec(const char *path){
    return (int32_t)pc_syscall(SYS_EXEC,(uint64_t)(uintptr_t)path,0,0);
}

int32_t pc_exec_with_args(const char *path, const char *arguments){
    return (int32_t)pc_syscall(SYS_EXEC,(uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)arguments,0);
}

int32_t pc_wait(int32_t pid, int32_t *status, bool nohang){
    return (int32_t)pc_syscall(SYS_WAIT,(uint32_t)pid,
        (uint64_t)(uintptr_t)status,nohang ? 1 : 0);
}

int32_t pc_try_getchar(void){
    return (int32_t)pc_syscall(SYS_TRY_GETCHAR,0,0,0);
}

int32_t pc_try_get_special(void){
    return (int32_t)pc_syscall(SYS_TRY_GET_SPECIAL,0,0,0);
}

int32_t pc_get_command_line(char *buffer, uint32_t capacity){
    return (int32_t)pc_syscall(SYS_GET_COMMAND_LINE,
        (uint64_t)(uintptr_t)buffer,capacity,0);
}

int32_t pc_get_process_name(char *buffer, uint32_t capacity){
    return (int32_t)pc_syscall(SYS_GET_PROCESS_NAME,
        (uint64_t)(uintptr_t)buffer,capacity,0);
}

int32_t pc_getenv(const char *name, char *buffer, uint32_t capacity){
    return (int32_t)pc_syscall(SYS_ENV_GET,(uint64_t)(uintptr_t)name,
        (uint64_t)(uintptr_t)buffer,capacity);
}

int32_t pc_setenv(const char *name, const char *value){
    return (int32_t)pc_syscall(SYS_ENV_SET,(uint64_t)(uintptr_t)name,
        (uint64_t)(uintptr_t)value,0);
}

int32_t pc_unsetenv(const char *name){
    return (int32_t)pc_syscall(SYS_ENV_UNSET,(uint64_t)(uintptr_t)name,0,0);
}

int32_t pc_listenv(struct process_environment_variable *variables,
                   uint32_t capacity){
    return (int32_t)pc_syscall(SYS_ENV_LIST,
        (uint64_t)(uintptr_t)variables,capacity,0);
}

int32_t pc_process_list(struct process_monitor_info *processes,
                        uint32_t capacity){
    return (int32_t)pc_syscall(SYS_PROCESS_LIST,
        (uint64_t)(uintptr_t)processes,capacity,0);
}

bool pc_cpu_info(struct cpu_monitor_info *info){
    return info && pc_syscall(SYS_CPU_INFO,(uint64_t)(uintptr_t)info,0,0)>=0;
}

bool pc_memory_info(struct memory_monitor_info *info){
    return info
        && pc_syscall(SYS_MEMORY_INFO,(uint64_t)(uintptr_t)info,0,0)>=0;
}

int32_t pc_ping(const char *target, uint16_t sequence, uint32_t timeout_ms,
                struct network_ping_result *result){
    if(!target || !result) return -1;
    struct network_ping_request request={
        .timeout_ms=timeout_ms,
        .sequence=sequence
    };
    pc_copy(request.target,target,sizeof(request.target));
    return (int32_t)pc_syscall(SYS_NET_PING,
        (uint64_t)(uintptr_t)&request,(uint64_t)(uintptr_t)result,0);
}

void *pc_heap_grow(uint64_t size){
    int64_t result=pc_syscall(SYS_HEAP_GROW,size,0,0);
    return result<0 ? 0 : (void*)(uintptr_t)result;
}

int32_t pc_file_open(const char *path){
    return (int32_t)pc_syscall(SYS_FILE_OPEN,(uint64_t)(uintptr_t)path,0,0);
}

int32_t pc_file_read(int32_t descriptor, void *buffer, uint32_t capacity){
    return (int32_t)pc_syscall(SYS_FILE_READ,(uint32_t)descriptor,
        (uint64_t)(uintptr_t)buffer,capacity);
}

int64_t pc_file_seek(int32_t descriptor, int64_t offset, uint32_t whence){
    return (int64_t)pc_syscall(SYS_FILE_SEEK,(uint32_t)descriptor,
        (uint64_t)offset,whence);
}

int32_t pc_file_stat(const char *path, struct file_stat_info *out){
    return (int32_t)pc_syscall(SYS_FILE_STAT,(uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)out,0);
}

int32_t pc_file_close(int32_t descriptor){
    return (int32_t)pc_syscall(SYS_FILE_CLOSE,(uint32_t)descriptor,0,0);
}

int32_t pc_file_write(const char *path, const void *buffer, uint32_t size){
    return (int32_t)pc_syscall(SYS_FILE_WRITE,(uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)buffer,size);
}

int32_t pc_directory_list(const char *path,
                          struct fs_directory_entry *entries,
                          uint32_t capacity){
    return (int32_t)pc_syscall(SYS_DIR_LIST,(uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)entries,capacity);
}

int32_t pc_file_create(const char *path){
    return (int32_t)pc_syscall(SYS_FILE_CREATE,
        (uint64_t)(uintptr_t)path,0,0);
}

int32_t pc_directory_create(const char *path){
    return (int32_t)pc_syscall(SYS_DIR_CREATE,
        (uint64_t)(uintptr_t)path,0,0);
}

int32_t pc_file_delete(const char *path){
    return (int32_t)pc_syscall(SYS_FILE_DELETE,
        (uint64_t)(uintptr_t)path,0,0);
}

int32_t pc_file_rename(const char *path, const char *new_name){
    return (int32_t)pc_syscall(SYS_FILE_RENAME,
        (uint64_t)(uintptr_t)path,(uint64_t)(uintptr_t)new_name,0);
}

int32_t pc_file_move(const char *path, const char *destination_directory){
    return (int32_t)pc_syscall(SYS_FILE_MOVE,
        (uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)destination_directory,0);
}

bool pc_file_exists(const char *path){
    int32_t descriptor=(int32_t)pc_syscall(SYS_OPEN,
        (uint64_t)(uintptr_t)path,0,0);
    if(descriptor<0) return false;
    (void)pc_syscall(SYS_CLOSE,(uint32_t)descriptor,0,0);
    return true;
}

bool pc_display_get_info(struct pc_display_info *info){
    if(!info) return false;
    struct framebuffer_info value;
    if(pc_syscall(SYS_FB_INFO,(uint64_t)(uintptr_t)&value,0,0)<0) return false;
    info->width=value.width;
    info->height=value.height;
    info->pitch=value.pitch;
    info->size_bytes=value.size_bytes;
    info->bpp=value.bpp;
    info->available=value.available!=0;
    return true;
}

void pc_display_begin_update(void){
    (void)pc_syscall(SYS_FB_BEGIN_UPDATE,0,0,0);
}

void pc_display_end_update(void){
    (void)pc_syscall(SYS_FB_END_UPDATE,0,0,0);
}

void pc_display_clear(uint32_t color){
    (void)pc_syscall(SYS_CLEAR,color,0,0);
}

void pc_desktop_redraw(void){
    (void)pc_syscall(SYS_DESKTOP_REDRAW,0,0,0);
}

bool pc_gui_window_register(const struct gui_window_request *request){
    return request && pc_syscall(SYS_GUI_WINDOW_REGISTER,
        (uint64_t)(uintptr_t)request,0,0)>=0;
}

bool pc_gui_window_update(const struct gui_window_request *request){
    return request && pc_syscall(SYS_GUI_WINDOW_UPDATE,
        (uint64_t)(uintptr_t)request,0,0)>=0;
}

void pc_gui_window_unregister(void){
    (void)pc_syscall(SYS_GUI_WINDOW_UNREGISTER,0,0,0);
}

uint32_t pc_gui_window_state(void){
    int64_t state=pc_syscall(SYS_GUI_WINDOW_STATE,0,0,0);
    return state<0 ? 0 : (uint32_t)state;
}

void pc_gui_window_repaint_done(void){
    (void)pc_syscall(SYS_GUI_WINDOW_REPAINT_DONE,0,0,0);
}

bool pc_console_configure(uint32_t x, uint32_t y,
                          uint32_t width, uint32_t height,
                          uint32_t foreground, uint32_t background){
    struct framebuffer_console_request request={
        .x=x,.y=y,.width=width,.height=height,
        .foreground=foreground,.background=background
    };
    return pc_syscall(SYS_CONSOLE_CONFIGURE,
        (uint64_t)(uintptr_t)&request,0,0)>=0;
}

void pc_console_clear(void){
    (void)pc_syscall(SYS_CONSOLE_CLEAR,0,0,0);
}

void pc_console_disable(void){
    (void)pc_syscall(SYS_CONSOLE_DISABLE,0,0,0);
}

void pc_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  uint32_t color){
    (void)syscall5(SYS_DRAW_RECT,x,y,width,height,color);
}

void pc_draw_text(uint32_t x, uint32_t y, const char *text,
                  uint32_t foreground, uint32_t background){
    struct framebuffer_text_request request={
        .x=x,.y=y,.text=text,.fg=foreground,.bg=background,.size=8
    };
    (void)pc_syscall(SYS_DRAW_TEXT,(uint64_t)(uintptr_t)&request,0,0);
}

void pc_draw_text_sized(uint32_t x, uint32_t y, const char *text,
                        uint32_t foreground, uint32_t background,
                        uint32_t size){
    if(size<8) size=8;
    if(size>48) size=48;
    struct framebuffer_text_request request={
        .x=x,.y=y,.text=text,.fg=foreground,.bg=background,.size=size
    };
    (void)pc_syscall(SYS_DRAW_TEXT_SIZED,(uint64_t)(uintptr_t)&request,0,0);
}

bool pc_mouse_get(struct mouse_state *state){
    return state && pc_syscall(SYS_GET_MOUSE,(uint64_t)(uintptr_t)state,0,0)>=0;
}

int32_t pc_list_disks(struct storage_device_info *devices, uint32_t capacity){
    return (int32_t)pc_syscall(SYS_DISK_LIST,
        (uint64_t)(uintptr_t)devices,capacity,0);
}

int32_t pc_install_start(const char *device, const char *serial){
    return (int32_t)pc_syscall(SYS_INSTALL_START,
        (uint64_t)(uintptr_t)device,(uint64_t)(uintptr_t)serial,0);
}

bool pc_install_status(struct install_status *status){
    return status && pc_syscall(SYS_INSTALL_STATUS,
        (uint64_t)(uintptr_t)status,0,0)>=0;
}

bool pc_install_log(struct install_log *log){
    return log && pc_syscall(SYS_INSTALL_LOG,
        (uint64_t)(uintptr_t)log,0,0)>=0;
}

bool pc_audio_get_status(struct audio_status *status){
    return status && pc_syscall(SYS_AUDIO_GET_STATUS,(uint64_t)(uintptr_t)status,0,0)>=0;
}
int32_t pc_audio_get_volume(void){ return (int32_t)pc_syscall(SYS_AUDIO_GET_VOLUME,0,0,0); }
bool pc_audio_is_muted(void){ return pc_syscall(SYS_AUDIO_IS_MUTED,0,0,0)!=0; }
void pc_audio_set_volume(uint32_t v){ (void)pc_syscall(SYS_AUDIO_SET_VOLUME,v,0,0); }
void pc_audio_set_muted(bool m){ (void)pc_syscall(SYS_AUDIO_SET_MUTED,m?1:0,0,0); }
void pc_audio_adjust_volume(int32_t d){ (void)pc_syscall(SYS_AUDIO_ADJUST_VOLUME,(uint64_t)(int64_t)d,0,0); }
bool pc_audio_select_output(uint32_t i){ return pc_syscall(SYS_AUDIO_SELECT_OUTPUT_DEVICE,i,0,0)==0; }
void pc_audio_play_test(void){ (void)pc_syscall(SYS_AUDIO_PLAY_TEST_SOUND,0,0,0); }
int32_t pc_audio_play_tone(uint32_t f,uint32_t d){ return (int32_t)pc_syscall(SYS_AUDIO_PLAY_TONE,f,d,0); }
void pc_audio_stop_tone(void){ (void)pc_syscall(SYS_AUDIO_STOP_TONE,0,0,0); }
int32_t pc_audio_pcm_push(const int16_t *s,uint32_t f){ return (int32_t)pc_syscall(SYS_AUDIO_PCM_PUSH,(uint64_t)(uintptr_t)s,f,0); }
void pc_audio_pcm_eos(void){ (void)pc_syscall(SYS_AUDIO_PCM_PUSH,0,0,0); }
int32_t pc_audio_pcm_start(void){ return (int32_t)pc_syscall(SYS_AUDIO_PCM_START,0,0,0); }
void pc_audio_pcm_stop(void){ (void)pc_syscall(SYS_AUDIO_PCM_STOP,0,0,0); }
int32_t pc_wifi_scan(void){ return (int32_t)pc_syscall(SYS_WIFI_SCAN,0,0,0); }
int32_t pc_wifi_list(struct wifi_network_info *networks, uint32_t capacity){
    return (int32_t)pc_syscall(SYS_WIFI_LIST,(uint64_t)(uintptr_t)networks,capacity,0);
}
int32_t pc_wifi_connect(const char *ssid, const char *password){
    if(!ssid) return -1;
    struct wifi_connect_request req={0};
    pc_copy(req.ssid, ssid, sizeof(req.ssid));
    if(password) pc_copy(req.password, password, sizeof(req.password));
    return (int32_t)pc_syscall(SYS_WIFI_CONNECT,(uint64_t)(uintptr_t)&req,0,0);
}
int32_t pc_wifi_disconnect(void){ return (int32_t)pc_syscall(SYS_WIFI_DISCONNECT,0,0,0); }
bool pc_wifi_status(struct wifi_status_info *status){
    return status && pc_syscall(SYS_WIFI_STATUS,(uint64_t)(uintptr_t)status,0,0)>=0;
}
int32_t pc_save_klog(const char *device, const char *path){
    if(!device || !path) return -1;
    struct save_klog_request req={0};
    pc_copy(req.device, device, sizeof(req.device));
    pc_copy(req.path, path, sizeof(req.path));
    return (int32_t)pc_syscall(SYS_SAVE_KLOG, (uint64_t)(uintptr_t)&req, 0, 0);
}
int32_t pc_get_root_device(char *buffer, uint32_t capacity){
    if(!buffer || !capacity) return -1;
    return (int32_t)pc_syscall(SYS_GET_ROOT_DEVICE, (uint64_t)(uintptr_t)buffer, capacity, 0);
}
int32_t pc_format_custom(const char *device, uint32_t partition_count, const uint64_t *sizes_gb){
    if(!device || !partition_count || !sizes_gb) return -1;
    struct fat32_custom_format_request req={0};
    pc_copy(req.device, device, sizeof(req.device));
    req.partition_count=partition_count;
    for(uint32_t i=0;i<partition_count && i<4;i++) req.sizes_gb[i]=sizes_gb[i];
    return (int32_t)pc_syscall(SYS_FAT32_FORMAT_CUSTOM, (uint64_t)(uintptr_t)&req, 0, 0);
}

int32_t pc_format_device_ex(const char *device, const char *serial, uint8_t fs_type){
    if(!device || !serial) return -1;
    struct format_request req={0};
    pc_copy(req.device, device, sizeof(req.device));
    pc_copy(req.serial, serial, sizeof(req.serial));
    pc_copy(req.erase, "ERASE", sizeof(req.erase));
    req.fs_type = fs_type;
    return (int32_t)pc_syscall(SYS_FORMAT_DEVICE_EX, (uint64_t)(uintptr_t)&req, 0, 0);
}

int32_t pc_install_start_ex(const char *device, const char *serial, uint8_t fs_type){
    if(!device || !serial) return -1;
    struct install_start_request req={0};
    pc_copy(req.device, device, sizeof(req.device));
    pc_copy(req.serial, serial, sizeof(req.serial));
    req.fs_type = fs_type;
    return (int32_t)pc_syscall(SYS_INSTALL_START_EX, (uint64_t)(uintptr_t)&req, 0, 0);
}

int32_t pc_get_fs_type(char *buffer, uint32_t capacity){
    if(!buffer || !capacity) return -1;
    return (int32_t)pc_syscall(SYS_GET_FS_TYPE, (uint64_t)(uintptr_t)buffer, capacity, 0);
}
int32_t pc_ext2_stat(const char *path, struct ext2_stat_info *out){
    if(!path || !out) return -1;
    return (int32_t)pc_syscall(SYS_EXT2_STAT,(uint64_t)(uintptr_t)path,(uint64_t)(uintptr_t)out,0);
}
int32_t pc_ext2_inode(uint32_t ino, struct ext2_stat_info *out){
    if(!out) return -1;
    return (int32_t)pc_syscall(SYS_EXT2_INODE,(uint64_t)ino,(uint64_t)(uintptr_t)out,0);
}
int32_t pc_ext2_super(struct ext2_super_info *out){
    if(!out) return -1;
    return (int32_t)pc_syscall(SYS_EXT2_SUPER,(uint64_t)(uintptr_t)out,0,0);
}
int32_t pc_ext2_blocks(const char *path, struct ext2_blocks_info *out){
    if(!path || !out) return -1;
    return (int32_t)pc_syscall(SYS_EXT2_BLOCKS,(uint64_t)(uintptr_t)path,(uint64_t)(uintptr_t)out,0);
}
void pc_exit(int32_t status){
    (void)pc_syscall(SYS_EXIT,(uint64_t)(int64_t)status,0,0);
    for(;;) __asm__ volatile("pause");
}
