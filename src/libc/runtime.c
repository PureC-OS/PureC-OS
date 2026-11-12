#include "include/purec.h"
#include "../kernel/syscall/syscall.h"
#include "../gfx/text.h"

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

int32_t pc_fork(void){ return (int32_t)pc_syscall(SYS_FORK,0,0,0); }

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

int32_t pc_key_event_get(struct key_event *event){
    return (int32_t)pc_syscall(SYS_KEY_EVENT_GET,(uint64_t)(uintptr_t)event,0,0);
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
    return pc_process_list_page(processes,capacity,0);
}

int32_t pc_process_list_page(struct process_monitor_info *processes,
                             uint32_t capacity, uint32_t offset){
    return (int32_t)pc_syscall(SYS_PROCESS_LIST,
        (uint64_t)(uintptr_t)processes,capacity,offset);
}

bool pc_cpu_info(struct cpu_monitor_info *info){
    return info && pc_syscall(SYS_CPU_INFO,(uint64_t)(uintptr_t)info,0,0)>=0;
}

bool pc_memory_info(struct memory_monitor_info *info){
    return info
        && pc_syscall(SYS_MEMORY_INFO,(uint64_t)(uintptr_t)info,0,0)>=0;
}

int32_t pc_cpu_core_info(struct cpu_core_info *info){
    if(!info) return -1;
    return (int32_t)pc_syscall(SYS_CPU_CORE_INFO,(uint64_t)(uintptr_t)info,0,0);
}

static uint64_t tcp_arg(int32_t handle, uint32_t timeout_ms){
    return (uint64_t)(uint32_t)handle|((uint64_t)timeout_ms<<32);
}

int32_t pc_tcp_socket(void){
    return (int32_t)pc_syscall(SYS_TCP_SOCKET,0,0,0);
}

int32_t pc_tcp_connect(int32_t handle, uint32_t ipv4, uint16_t port,
                       uint32_t timeout_ms){
    return (int32_t)pc_syscall(SYS_TCP_CONNECT,tcp_arg(handle,timeout_ms),
                               ipv4,port);
}

int32_t pc_tcp_listen(int32_t handle, uint16_t port, uint8_t backlog){
    return (int32_t)pc_syscall(SYS_TCP_LISTEN,tcp_arg(handle,0),port,backlog);
}

int32_t pc_tcp_accept(int32_t handle, uint32_t timeout_ms){
    return (int32_t)pc_syscall(SYS_TCP_ACCEPT,tcp_arg(handle,timeout_ms),0,0);
}

int32_t pc_tcp_send(int32_t handle, const void *data, uint32_t length,
                    uint32_t timeout_ms){
    if(length && !data) return -1;
    return (int32_t)pc_syscall(SYS_TCP_SEND,tcp_arg(handle,timeout_ms),
                               (uint64_t)(uintptr_t)data,length);
}

int32_t pc_tcp_recv(int32_t handle, void *data, uint32_t length,
                    uint32_t timeout_ms){
    if(length && !data) return -1;
    return (int32_t)pc_syscall(SYS_TCP_RECV,tcp_arg(handle,timeout_ms),
                               (uint64_t)(uintptr_t)data,length);
}

int32_t pc_tcp_close(int32_t handle){
    return (int32_t)pc_syscall(SYS_TCP_CLOSE,tcp_arg(handle,0),0,0);
}

int32_t pc_tcp_abort(int32_t handle){
    return (int32_t)pc_syscall(SYS_TCP_ABORT,tcp_arg(handle,0),0,0);
}

int32_t pc_tcp_info(int32_t handle, struct net_tcp_info *info){
    if(!info) return -1;
    return (int32_t)pc_syscall(SYS_TCP_INFO,tcp_arg(handle,0),
                               (uint64_t)(uintptr_t)info,0);
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

int32_t pc_session_begin(uint32_t uid, uint32_t gid){
    return (int32_t)pc_syscall(SYS_SESSION_BEGIN,uid,gid,0);
}

int32_t pc_install_start_accounts(const char *device, const char *serial,
                                  uint8_t fs_type,
                                  const struct install_account_request *accounts){
    if(!device || !serial) return -1;
    struct install_start_request req={0};
    pc_copy(req.device, device, sizeof(req.device));
    pc_copy(req.serial, serial, sizeof(req.serial));
    req.fs_type = fs_type;
    if(accounts) req.accounts = *accounts;
    return (int32_t)pc_syscall(SYS_INSTALL_START_EX, (uint64_t)(uintptr_t)&req, 0, 0);
}

static struct {
    uint32_t *px;
    uint32_t w, h;
    int32_t sx, sy;
    int32_t depth;
    bool dirty;
    int32_t dx0, dy0, dx1, dy1;
} pc_surf;

static int32_t pc_raw_blit(const uint32_t *pixels, uint32_t stride, uint32_t width,
                           uint32_t height, int32_t x, int32_t y, uint32_t flags);

void pc_surface_attach(uint32_t *pixels, uint32_t width, uint32_t height, int32_t x, int32_t y){
    pc_surf.px=pixels;
    pc_surf.w=width;
    pc_surf.h=height;
    pc_surf.sx=x;
    pc_surf.sy=y;
    pc_surf.depth=0;
    pc_surf.dirty=false;
}

void pc_surface_move(int32_t x, int32_t y){
    pc_surf.sx=x;
    pc_surf.sy=y;
}

void pc_surface_detach(void){
    pc_surf.px=0;
    pc_surf.dirty=false;
    pc_surf.depth=0;
}

uint32_t *pc_surface_pixels(uint32_t *width, uint32_t *height){
    if(width) *width=pc_surf.w;
    if(height) *height=pc_surf.h;
    return pc_surf.px;
}

static void pc_surface_flush(void){
    if(!pc_surf.px || !pc_surf.dirty) return;
    pc_surf.dirty=false;
    (void)pc_raw_blit(pc_surf.px+(uint64_t)pc_surf.dy0*pc_surf.w+(uint32_t)pc_surf.dx0,pc_surf.w,
                      (uint32_t)(pc_surf.dx1-pc_surf.dx0),(uint32_t)(pc_surf.dy1-pc_surf.dy0),
                      pc_surf.sx+pc_surf.dx0,pc_surf.sy+pc_surf.dy0,1u);
}

void pc_surface_damage(int32_t x, int32_t y, uint32_t width, uint32_t height){
    if(!pc_surf.px || !width || !height) return;
    int32_t x0=x<0?0:x, y0=y<0?0:y;
    int32_t x1=x+(int32_t)width, y1=y+(int32_t)height;
    if(x1>(int32_t)pc_surf.w) x1=(int32_t)pc_surf.w;
    if(y1>(int32_t)pc_surf.h) y1=(int32_t)pc_surf.h;
    if(x0>=x1 || y0>=y1) return;
    if(!pc_surf.dirty){
        pc_surf.dx0=x0; pc_surf.dy0=y0; pc_surf.dx1=x1; pc_surf.dy1=y1;
        pc_surf.dirty=true;
    } else {
        if(x0<pc_surf.dx0) pc_surf.dx0=x0;
        if(y0<pc_surf.dy0) pc_surf.dy0=y0;
        if(x1>pc_surf.dx1) pc_surf.dx1=x1;
        if(y1>pc_surf.dy1) pc_surf.dy1=y1;
    }
    if(pc_surf.depth==0) pc_surface_flush();
}

void pc_surface_present(void){
    pc_surface_flush();
}

static bool pc_surface_clip(int64_t *x, int64_t *y, int64_t *w, int64_t *h, int64_t *ox, int64_t *oy){
    int64_t x0=*x-pc_surf.sx, y0=*y-pc_surf.sy;
    int64_t x1=x0+*w, y1=y0+*h;
    *ox=0; *oy=0;
    if(x0<0){ *ox=-x0; x0=0; }
    if(y0<0){ *oy=-y0; y0=0; }
    if(x1>(int64_t)pc_surf.w) x1=pc_surf.w;
    if(y1>(int64_t)pc_surf.h) y1=pc_surf.h;
    if(x0>=x1 || y0>=y1) return false;
    *x=x0; *y=y0; *w=x1-x0; *h=y1-y0;
    return true;
}

static int32_t pc_raw_blit(const uint32_t *pixels, uint32_t stride, uint32_t width,
                           uint32_t height, int32_t x, int32_t y, uint32_t flags){
    struct fb_blit_request req={
        .pixels=(uint64_t)(uintptr_t)pixels,
        .stride=stride,
        .width=width,
        .height=height,
        .x=x,
        .y=y,
        .flags=flags};
    return (int32_t)pc_syscall(SYS_FB_BLIT,(uint64_t)(uintptr_t)&req,0,0);
}

int32_t pc_fb_blit(const uint32_t *pixels, uint32_t stride, uint32_t width,
                   uint32_t height, int32_t x, int32_t y, uint32_t flags){
    if(!pc_surf.px || !pixels)
        return pc_raw_blit(pixels,stride,width,height,x,y,flags);
    int64_t bx=x, by=y, bw=width, bh=height, ox, oy;
    if(!pc_surface_clip(&bx,&by,&bw,&bh,&ox,&oy)) return 0;
    bool keep=(flags&1u)!=0;
    for(int64_t row=0;row<bh;row++){
        const uint32_t *s=pixels+(uint64_t)(row+oy)*stride+(uint64_t)ox;
        uint32_t *d=pc_surf.px+(uint64_t)(by+row)*pc_surf.w+(uint64_t)bx;
        if(keep){
            for(int64_t i=0;i<bw;i++) if(s[i]>>24) d[i]=s[i];
        } else {
            for(int64_t i=0;i<bw;i++) d[i]=s[i]|0xFF000000u;
        }
    }
    pc_surface_damage((int32_t)bx,(int32_t)by,(uint32_t)bw,(uint32_t)bh);
    return 0;
}

int32_t pc_time_get(struct time_info *out){
    return (int32_t)pc_syscall(SYS_TIME_GET,(uint64_t)(uintptr_t)out,0,0);
}

int32_t pc_time_zone_get(char *out, uint32_t capacity){
    if(!out || capacity==0) return -1;
    out[0]=0;
    return (int32_t)pc_syscall(SYS_TIME_ZONE,TIME_ZONE_GET,(uint64_t)(uintptr_t)out,capacity);
}

int32_t pc_time_zone_set(const char *name){
    return (int32_t)pc_syscall(SYS_TIME_ZONE,TIME_ZONE_SET,(uint64_t)(uintptr_t)name,0);
}

int32_t pc_time_set(int64_t unix_seconds, int32_t tz_minutes){
    return (int32_t)pc_syscall(SYS_TIME_SET,(uint64_t)unix_seconds,
        (uint64_t)(int64_t)tz_minutes,0);
}

int32_t pc_file_stat_ex(const char *path, struct file_stat_ex *out){
    return (int32_t)pc_syscall(SYS_FILE_STAT_EX,(uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)out,0);
}

int32_t pc_getrandom(void *buffer, uint32_t count){
    return (int32_t)pc_syscall(SYS_GETRANDOM,(uint64_t)(uintptr_t)buffer,count,0);
}

int32_t pc_getuid(void){ return (int32_t)pc_syscall(SYS_GETUID,0,0,0); }
static int32_t config_uid=-1;
static char config_dir[128];

static uint32_t config_join(char *out, uint32_t cap, uint32_t at, const char *text){
    while(text && *text && at+1<cap) out[at++]=*text++;
    if(at<cap) out[at]='\0';
    return at;
}

int32_t pc_config_path(const char *name, char *out, uint32_t capacity, bool for_write){
    if(!name || !out || capacity<2) return -1;
    int32_t uid=pc_getuid();
    if(uid!=config_uid){
        struct user_info info;
        config_dir[0]='\0';
        if(pc_user_lookup(0,(uint32_t)uid,&info)==0 && info.home[0]){
            uint32_t n=config_join(config_dir,sizeof(config_dir),0,info.home);
            config_join(config_dir,sizeof(config_dir),n,"/.config/purec");
        }
        config_uid=uid;
    }
    if(config_dir[0]){
        uint32_t n=config_join(out,capacity,0,config_dir);
        n=config_join(out,capacity,n,"/");
        config_join(out,capacity,n,name);
        if(for_write){
            char parent[128];
            uint32_t len=config_join(parent,sizeof(parent),0,config_dir);
            while(len && parent[len-1]!='/') len--;
            if(len){
                parent[len-1]='\0';
                (void)pc_directory_create(parent);
            }
            (void)pc_directory_create(config_dir);
            return 0;
        }
        int32_t fd=pc_file_open(out);
        if(fd>=0){
            pc_file_close(fd);
            return 0;
        }
    }
    if(for_write) (void)pc_directory_create("/config");
    uint32_t n=config_join(out,capacity,0,"/config/");
    config_join(out,capacity,n,name);
    return 1;
}

int32_t pc_sudo(const char *password){ return (int32_t)pc_syscall(SYS_SUDO,(uint64_t)(uintptr_t)password,0,0); }
int32_t pc_getgid(void){ return (int32_t)pc_syscall(SYS_GETGID,0,0,0); }
int32_t pc_setuid(uint32_t uid){ return (int32_t)pc_syscall(SYS_SETUID,uid,0,0); }
int32_t pc_setgid(uint32_t gid){ return (int32_t)pc_syscall(SYS_SETGID,gid,0,0); }

int32_t pc_chmod(const char *path, uint32_t mode){
    return (int32_t)pc_syscall(SYS_CHMOD,(uint64_t)(uintptr_t)path,mode,0);
}

int32_t pc_utimes(const char *path, uint32_t atime, uint32_t mtime){
    return (int32_t)pc_syscall(SYS_UTIMES,(uint64_t)(uintptr_t)path,atime,mtime);
}

int32_t pc_chown(const char *path, uint32_t uid, uint32_t gid){
    return (int32_t)pc_syscall(SYS_CHOWN,(uint64_t)(uintptr_t)path,uid,gid);
}

int32_t pc_file_perm(const char *path, struct file_perm_info *out){
    return (int32_t)pc_syscall(SYS_FILE_PERM,(uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)out,0);
}

int32_t pc_user_lookup(const char *name, uint32_t uid, struct user_info *out){
    return (int32_t)pc_syscall(SYS_USER_LOOKUP,(uint64_t)(uintptr_t)name,uid,
        (uint64_t)(uintptr_t)out);
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

int32_t pc_directory_list_long(const char *path,
                          struct fs_directory_entry_long *entries,
                          uint32_t capacity){
    return (int32_t)pc_syscall(SYS_DIR_LIST_LONG,(uint64_t)(uintptr_t)path,
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

int32_t pc_ipc_send(int32_t dest_pid, const void *buffer, uint32_t size){
    return (int32_t)pc_syscall(SYS_IPC_SEND,
        (uint64_t)(uint32_t)dest_pid,(uint64_t)(uintptr_t)buffer,size);
}

int32_t pc_ipc_recv(void *buffer, uint32_t capacity, uint32_t wait_ms){
    return (int32_t)pc_syscall(SYS_IPC_RECV,
        (uint64_t)(uintptr_t)buffer,capacity,wait_ms);
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

int32_t pc_openpty(int32_t fds[2]){
    return (int32_t)pc_syscall(SYS_OPENPTY,(uint64_t)(uintptr_t)fds,0,0);
}

int32_t pc_tty_ioctl(int32_t fd, uint32_t request, void *argument){
    return (int32_t)pc_syscall(SYS_TTY_IOCTL,request,(uint64_t)(uintptr_t)argument,(uint64_t)fd);
}

void pc_display_begin_update(void){
    if(pc_surf.px){
        pc_surf.depth++;
        return;
    }
    (void)pc_syscall(SYS_FB_BEGIN_UPDATE,0,0,0);
}

void pc_display_end_update(void){
    if(pc_surf.px){
        if(pc_surf.depth>0) pc_surf.depth--;
        if(pc_surf.depth==0) pc_surface_flush();
        return;
    }
    (void)pc_syscall(SYS_FB_END_UPDATE,0,0,0);
}

void pc_display_clear(uint32_t color){
    (void)pc_syscall(SYS_CLEAR,color,0,0);
}

int32_t pc_display_set_mode_scale(uint32_t width, uint32_t height,
                                  uint32_t bpp, uint32_t scale_percent){
    return (int32_t)syscall5(SYS_DISPLAY_SET_MODE,width,height,bpp,
                             scale_percent,0);
}

int32_t pc_display_set_mode(uint32_t width, uint32_t height, uint32_t bpp){
    return pc_display_set_mode_scale(width,height,bpp,0);
}

bool pc_display_scale_info(struct pc_display_scale *out){
    if(!out) return false;
    struct display_scale_info value;
    if(pc_syscall(SYS_DISPLAY_SCALE_INFO,(uint64_t)(uintptr_t)&value,0,0)<0)
        return false;
    out->physical_width=value.physical_width;
    out->physical_height=value.physical_height;
    out->scale_percent=value.scale_percent;
    return true;
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

bool pc_gui_window_info(struct gui_window_info *info){
    return info && pc_syscall(SYS_GUI_WINDOW_INFO,
        (uint64_t)(uintptr_t)info,0,0)>=0;
}

bool pc_gui_window_set_flags(uint32_t flags){
    return pc_syscall(SYS_GUI_WINDOW_FLAGS,flags,0,0)>=0;
}

bool pc_gui_window_set_title(const char *title){
    return title && pc_syscall(SYS_GUI_WINDOW_TITLE,
        (uint64_t)(uintptr_t)title,0,0)>=0;
}

bool pc_gui_window_command(uint32_t command){
    return pc_syscall(SYS_GUI_WINDOW_CMD,command,0,0)>=0;
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
    if(!pc_surf.px){
        (void)syscall5(SYS_DRAW_RECT,x,y,width,height,color);
        return;
    }
    int64_t bx=x, by=y, bw=width, bh=height, ox, oy;
    if(!pc_surface_clip(&bx,&by,&bw,&bh,&ox,&oy)) return;
    uint32_t c=color|0xFF000000u;
    for(int64_t row=0;row<bh;row++){
        uint32_t *d=pc_surf.px+(uint64_t)(by+row)*pc_surf.w+(uint64_t)bx;
        for(int64_t i=0;i<bw;i++) d[i]=c;
    }
    pc_surface_damage((int32_t)bx,(int32_t)by,(uint32_t)bw,(uint32_t)bh);
}

static void pc_rect_cb(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t color, void *ctx){
    (void)ctx;
    if(w && h) pc_draw_rect(x, y, w, h, color);
}

void pc_draw_text(uint32_t x, uint32_t y, const char *text,
                  uint32_t foreground, uint32_t background){
    if(!text || !*text) return;
    gfx_draw_text_opaque(text, x, y, foreground, background, 8,
                         GFX_FONT_CLASSIC, pc_rect_cb, 0);
}

void pc_draw_text_sized(uint32_t x, uint32_t y, const char *text,
                        uint32_t foreground, uint32_t background,
                        uint32_t size){
    if(!text || !*text) return;
    gfx_draw_text_opaque(text, x, y, foreground, background, size,
                         GFX_FONT_CLASSIC, pc_rect_cb, 0);
}

void pc_draw_text_tr(uint32_t x, uint32_t y, const char *text,
                     uint32_t foreground){
    if(!text || !*text) return;
    gfx_draw_text(text, x, y, foreground, 8,
                  GFX_FONT_CLASSIC, pc_rect_cb, 0);
}

void pc_draw_text_sized_tr(uint32_t x, uint32_t y, const char *text,
                           uint32_t foreground, uint32_t size){
    if(!text || !*text) return;
    gfx_draw_text(text, x, y, foreground, size,
                  GFX_FONT_CLASSIC, pc_rect_cb, 0);
}

bool pc_mouse_get(struct mouse_state *state){
    return state && pc_syscall(SYS_GET_MOUSE,(uint64_t)(uintptr_t)state,0,0)>=0;
}

int32_t pc_list_disks(struct storage_device_info *devices, uint32_t capacity){
    return (int32_t)pc_syscall(SYS_DISK_LIST,
        (uint64_t)(uintptr_t)devices,capacity,0);
}

void pc_reboot(void){
    (void)pc_syscall(SYS_REBOOT,0,0,0);
}

void pc_shutdown(void){
    (void)pc_syscall(SYS_SHUTDOWN,0,0,0);
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
int32_t pc_net_if_list(struct net_if_info *infos, uint32_t capacity){
    if(capacity>NET_IF_MAX_COUNT) return -1;
    return (int32_t)pc_syscall(SYS_NET_IF_LIST,(uint64_t)(uintptr_t)infos,capacity,0);
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
    struct custom_format_request req={0};
    pc_copy(req.device, device, sizeof(req.device));
    req.partition_count=partition_count;
    for(uint32_t i=0;i<partition_count && i<4;i++) req.sizes_gb[i]=sizes_gb[i];
    return (int32_t)pc_syscall(SYS_FS_FORMAT_CUSTOM, (uint64_t)(uintptr_t)&req, 0, 0);
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
