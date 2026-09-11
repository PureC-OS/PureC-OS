#include "process.h"
#include "elf.h"
#include "scheduler.h"
#include "../syscall/syscall.h"
#include "../diagnostics/klog.h"
#include "../diagnostics/panic.h"
#include "program_alias.h"
#include "../../boot/install_source.h"
#include "../../fs/vfs.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../lib/string.h"
#include "../../userspace/window_manager.h"

#define USER_STACK_TOP 0x00007FFFFFF00000ULL
#define USER_STACK_PAGES 16
#define USER_HEAP_GUARD_PAGES 1
#define USER_PROCESS_PRIORITY 1

extern void arch_enter_user(uint64_t instruction_pointer,
                            uint64_t stack_pointer) __attribute__((noreturn));

static struct process processes[PROCESS_MAX_COUNT];
static uint32_t next_pid=1;
static uint64_t process_sample_tick;

static bool environment_name_valid(const char *name){
    if(!name || !name[0]) return false;
    for(uint32_t index=0;name[index];index++){
        char character=name[index];
        if(index>=PROCESS_ENVIRONMENT_NAME_CAPACITY-1
           || !((character>='a' && character<='z')
                || (character>='A' && character<='Z')
                || character=='_' || (index>0 && character>='0'
                                      && character<='9'))) return false;
    }
    return true;
}

static int32_t environment_find(const struct process *process,
                                const char *name){
    for(uint32_t index=0;index<PROCESS_ENVIRONMENT_COUNT;index++){
        if(process->environment[index].used
           && strcmp(process->environment[index].name,name)==0)
            return (int32_t)index;
    }
    return -1;
}

static void environment_put(struct process *process, const char *name,
                            const char *value){
    int32_t slot=environment_find(process,name);
    if(slot<0){
        for(uint32_t index=0;index<PROCESS_ENVIRONMENT_COUNT;index++){
            if(!process->environment[index].used){
                slot=(int32_t)index;
                break;
            }
        }
    }
    if(slot<0) return;
    struct process_environment_entry *entry=&process->environment[slot];
    entry->used=true;
    strncpy(entry->name,name,sizeof(entry->name)-1);
    entry->name[sizeof(entry->name)-1]='\0';
    strncpy(entry->value,value,sizeof(entry->value)-1);
    entry->value[sizeof(entry->value)-1]='\0';
}

static void environment_initialize(struct process *process,
                                   const struct process *parent){
    if(parent){
        memcpy(process->environment,parent->environment,
               sizeof(process->environment));
        return;
    }
    environment_put(process,"HOME","/");
    environment_put(process,"PWD","/");
    environment_put(process,"USER","purec");
    environment_put(process,"SHELL","/bin/program/terminal");
    environment_put(process,"PATH","/bin/program:/bin");
}

static struct process *allocate_process(void){
    for(uint32_t index=0;index<PROCESS_MAX_COUNT;index++){
        if(processes[index].state==PROCESS_FREE){
            struct process *process=&processes[index];
            memset(process,0,sizeof(*process));
            process->waiter_thread_id=-1;
            for(uint32_t fd=0;fd<PROCESS_FD_COUNT;fd++) process->descriptors[fd]=-1;
            process->descriptors[0]=VFS_FD_STDIN;
            process->descriptors[1]=VFS_FD_STDOUT;
            process->descriptors[2]=VFS_FD_STDERR;
            return process;
        }
    }
    return 0;
}

static void user_process_entry(void *argument){
    struct process *process=(struct process*)argument;
    process->state=PROCESS_RUNNING;
    arch_enter_user(process->entry,process->user_stack_top);
}

void process_init(void){
    memset(processes,0,sizeof(processes));
    next_pid=1;
    klog(KLOG_OK,"process: table initialized");
}

int32_t process_spawn_elf(const void *image, uint64_t image_size,
                          const char *name, const char *command_line){
    struct process *parent=process_current();
    struct process *process=allocate_process();
    if(!process) return -1;
    process->address_space=vmm_create_address_space();
    if(!process->address_space) return -1;
    struct elf_load_result loaded;
    if(!elf_load_user_image(image,image_size,process->address_space,&loaded)){
        vmm_destroy_address_space(process->address_space);
        process->address_space=0;
        process->state=PROCESS_FREE;
        return -1;
    }
    uint64_t stack_base=USER_STACK_TOP-USER_STACK_PAGES*PMM_PAGE_SIZE;
    uint64_t heap_base=loaded.highest_address
        +USER_HEAP_GUARD_PAGES*PMM_PAGE_SIZE;
    uint64_t heap_limit=stack_base-USER_HEAP_GUARD_PAGES*PMM_PAGE_SIZE;
    if(heap_base>=heap_limit){
        vmm_destroy_address_space(process->address_space);
        process->address_space=0;
        process->state=PROCESS_FREE;
        return -1;
    }
    if(!vmm_map_new_pages(process->address_space,stack_base,USER_STACK_PAGES,
                          VMM_PAGE_USER|VMM_PAGE_WRITABLE|VMM_PAGE_NX)){
        vmm_destroy_address_space(process->address_space);
        process->address_space=0;
        process->state=PROCESS_FREE;
        return -1;
    }
    process->pid=next_pid++;
    process->parent_pid=(uint32_t)(process_current_pid()>0
        ? process_current_pid() : 0);
    process->state=PROCESS_READY;
    process->entry=loaded.entry;
    process->user_stack_top=USER_STACK_TOP-8;
    process->heap_base=heap_base;
    process->heap_break=heap_base;
    process->heap_mapped_end=heap_base;
    process->heap_limit=heap_limit;
    environment_initialize(process,parent);
    if(command_line){
        strncpy(process->command_line,command_line,
                sizeof(process->command_line)-1);
        process->command_line[sizeof(process->command_line)-1]='\0';
    }
    strncpy(process->name,name ? name : "process",sizeof(process->name)-1);
    if(name && (strcmp(name,"installer")==0 || strcmp(name,"disks")==0))
        process->capabilities|=PROCESS_CAP_STORAGE_ADMIN;
    process->thread_id=scheduler_create_user_thread(
        user_process_entry,process,process->name,USER_PROCESS_PRIORITY,-1,
        process->address_space,process);
    if(process->thread_id<0){
        vmm_destroy_address_space(process->address_space);
        process->address_space=0;
        process->state=PROCESS_FREE;
        return -1;
    }
    klogf(KLOG_OK,"process: pid=%u name=%s entry=0x%llx cr3=0x%llx",
          process->pid,process->name,process->entry,process->address_space);
    return (int32_t)process->pid;
}

int32_t process_spawn_module(const char *path, const char *command_line){
    const void *image=NULL;
    uint64_t size=0;
    const char *module_path=path;
    if(!path) return -1;

    const char *name=path;
    for(const char *cursor=path;*cursor;cursor++){
        if(*cursor=='/' && cursor[1]) name=cursor+1;
    }

    if(boot_get_module(module_path,&image,&size)){
        return process_spawn_elf(image,size,name,command_line);
    }
    if(program_alias_resolve(path,&module_path)
       && boot_get_module(module_path,&image,&size)){
        return process_spawn_elf(image,size,name,command_line);
    }

    // Fallback: load executable binary from VFS
    int32_t fd=vfs_open(path);
    if(fd<0 && module_path && module_path!=path){
        fd=vfs_open(module_path);
    }
    if(fd>=0){
        uint64_t pages=2048; // 8MB buffer
        uint64_t phys=pmm_allocate_contiguous(pages);
        if(phys){
            void *vbuf=pmm_physical_to_virtual(phys);
            int32_t read_bytes=vfs_read(fd,vbuf,8*1024*1024);
            vfs_close(fd);
            if(read_bytes>0){
                int32_t pid=process_spawn_elf(vbuf,(uint64_t)read_bytes,name,command_line);
                pmm_free_contiguous(phys,pages);
                return pid;
            }
            pmm_free_contiguous(phys,pages);
            return -1;
        }
        vfs_close(fd);
    }
    return -1;
}

int32_t process_wait(uint32_t pid, int32_t *status, bool nohang){
    if(!pid) return -1;
    for(;;){
        struct process *target=0;
        for(uint32_t index=0;index<PROCESS_MAX_COUNT;index++){
            if(processes[index].state!=PROCESS_FREE
               && processes[index].pid==pid){
                target=&processes[index];
                break;
            }
        }
        if(!target) return -1;
        int32_t caller=process_current_pid();
        if(caller>0 && target->parent_pid!=(uint32_t)caller) return -1;
        if(target->state==PROCESS_EXITED){
            if(status) *status=target->exit_code;
            vmm_destroy_address_space(target->address_space);
            target->address_space=0;
            target->state=PROCESS_FREE;
            return (int32_t)pid;
        }
        if(nohang) return 0;
        int32_t waiter_thread_id=scheduler_current_tid();
        if(waiter_thread_id<0) return -1;
        if(target->waiter_thread_id>=0
           && target->waiter_thread_id!=waiter_thread_id) return -1;
        target->waiter_thread_id=waiter_thread_id;
        /* Waiting must remove the caller from the ready queue. A yielding
           high-priority parent otherwise starves its lower-priority child. */
        scheduler_block();
    }
}

struct process *process_current(void){
    struct thread *thread=scheduler_current_thread();
    return thread ? thread->process : 0;
}

int32_t process_current_pid(void){
    struct process *process=process_current();
    return process ? (int32_t)process->pid : 0;
}

bool process_current_is_user(void){
    struct thread *thread=scheduler_current_thread();
    return thread && thread->user_mode;
}

bool process_has_capability(uint32_t capability){
    struct process *process=process_current();
    return !process || (process->capabilities&capability)==capability;
}

uint64_t process_current_address_space(void){
    struct process *process=process_current();
    return process ? process->address_space : vmm_kernel_address_space();
}

uint64_t process_heap_grow(uint64_t size){
    struct process *process=process_current();
    if(!process || !process_current_is_user()) return 0;
    if(!size) return process->heap_break;
    if(size>process->heap_limit-process->heap_break) return 0;
    uint64_t previous_break=process->heap_break;
    uint64_t requested_break=previous_break+size;
    uint64_t requested_mapping=(requested_break+PMM_PAGE_SIZE-1)
        &~(PMM_PAGE_SIZE-1);
    while(process->heap_mapped_end<requested_mapping){
        if(!vmm_map_new_pages(process->address_space,
                              process->heap_mapped_end,1,
                              VMM_PAGE_USER|VMM_PAGE_WRITABLE|VMM_PAGE_NX)){
            return 0;
        }
        process->heap_mapped_end+=PMM_PAGE_SIZE;
    }
    process->heap_break=requested_break;
    return previous_break;
}

void process_exit_current(int32_t status){
    struct process *process=process_current();
    if(process){
        if(process->pid==1) kernel_panic("PID 1 exited");
        process->exit_code=status;
        process->runtime_ticks=scheduler_thread_runtime_ticks(
            process->thread_id);
        process->state=PROCESS_EXITED;
        window_manager_unregister(process->pid);
        for(uint32_t fd=3;fd<PROCESS_FD_COUNT;fd++){
            if(process->descriptors[fd]>=VFS_FD_BASE){
                (void)vfs_close(process->descriptors[fd]);
                process->descriptors[fd]=-1;
            }
        }
        klogf(KLOG_INFO,"process: pid=%u exited status=%d",process->pid,status);
        if(process->waiter_thread_id>=0)
            scheduler_unblock(process->waiter_thread_id);
    }
    scheduler_exit();
    __builtin_unreachable();
}

int32_t process_monitor_list(struct process_monitor_info *entries,
                             uint32_t capacity){
    uint64_t now=scheduler_total_ticks();
    uint64_t elapsed=now-process_sample_tick;
    uint32_t count=0;
    for(uint32_t index=0;index<PROCESS_MAX_COUNT;index++){
        struct process *process=&processes[index];
        if(process->state==PROCESS_FREE) continue;
        uint64_t runtime=process->state==PROCESS_EXITED
            ? process->runtime_ticks
            : scheduler_thread_runtime_ticks(process->thread_id);
        if(count<capacity){
            struct process_monitor_info *entry=&entries[count];
            memset(entry,0,sizeof(*entry));
            entry->pid=process->pid;
            entry->parent_pid=process->parent_pid;
            entry->state=(uint32_t)process->state;
            entry->exit_code=process->exit_code;
            entry->runtime_ms=runtime;
            entry->resident_bytes=vmm_user_page_count(process->address_space)
                *PMM_PAGE_SIZE;
            uint64_t delta=runtime-process->sampled_runtime_ticks;
            entry->cpu_percent=elapsed
                ? (uint32_t)((delta*100)/elapsed) : 0;
            if(entry->cpu_percent>100) entry->cpu_percent=100;
            strncpy(entry->name,process->name,sizeof(entry->name)-1);
        }
        process->sampled_runtime_ticks=runtime;
        count++;
    }
    process_sample_tick=now;
    return (int32_t)count;
}

int32_t process_fd_install(int32_t kernel_descriptor){
    struct process *process=process_current();
    if(!process) return kernel_descriptor;
    for(int32_t fd=3;fd<PROCESS_FD_COUNT;fd++){
        if(process->descriptors[fd]<0){
            process->descriptors[fd]=kernel_descriptor;
            return fd;
        }
    }
    return -1;
}

int32_t process_fd_resolve(int32_t descriptor){
    struct process *process=process_current();
    if(!process) return descriptor;
    if(descriptor<0 || descriptor>=PROCESS_FD_COUNT) return -1;
    return process->descriptors[descriptor];
}

int32_t process_fd_close(int32_t descriptor){
    struct process *process=process_current();
    if(!process) return vfs_close(descriptor);
    if(descriptor<3 || descriptor>=PROCESS_FD_COUNT
       || process->descriptors[descriptor]<VFS_FD_BASE) return -1;
    int32_t result=vfs_close(process->descriptors[descriptor]);
    process->descriptors[descriptor]=-1;
    return result;
}

bool process_user_buffer(const void *buffer, uint64_t size, bool writable){
    if(size==0) return true;
    if(!process_current_is_user()) return buffer != NULL;
    return buffer && vmm_user_range_accessible(process_current_address_space(),
        (uint64_t)(uintptr_t)buffer,size,writable);
}

bool process_user_string(const char *text, uint64_t capacity){
    if(!process_current_is_user()) return text!=0;
    if(!text || !capacity) return false;
    for(uint64_t index=0;index<capacity;index++){
        if(!process_user_buffer(text+index,1,false)) return false;
        if(text[index]=='\0') return true;
    }
    return false;
}

int32_t process_command_line(char *buffer, uint32_t capacity){
    struct process *process=process_current();
    if(!process || !buffer || !capacity) return -1;
    uint32_t length=(uint32_t)strlen(process->command_line);
    if(length+1>capacity) return -1;
    memcpy(buffer,process->command_line,length+1);
    return (int32_t)length;
}

int32_t process_name(char *buffer, uint32_t capacity){
    struct process *process=process_current();
    if(!process || !buffer || !capacity) return -1;
    uint32_t length=(uint32_t)strlen(process->name);
    if(length+1>capacity) return -1;
    memcpy(buffer,process->name,length+1);
    return (int32_t)length;
}

int32_t process_environment_get(const char *name, char *buffer,
                                uint32_t capacity){
    struct process *process=process_current();
    if(!process || !environment_name_valid(name) || !buffer || !capacity)
        return -1;
    int32_t slot=environment_find(process,name);
    if(slot<0) return -1;
    uint32_t length=(uint32_t)strlen(process->environment[slot].value);
    if(length+1>capacity) return -1;
    memcpy(buffer,process->environment[slot].value,length+1);
    return (int32_t)length;
}

int32_t process_environment_set(const char *name, const char *value){
    struct process *process=process_current();
    if(!process || !environment_name_valid(name) || !value
       || strlen(value)>=PROCESS_ENVIRONMENT_VALUE_CAPACITY) return -1;
    int32_t slot=environment_find(process,name);
    if(slot<0){
        for(uint32_t index=0;index<PROCESS_ENVIRONMENT_COUNT;index++){
            if(!process->environment[index].used){
                slot=(int32_t)index;
                break;
            }
        }
    }
    if(slot<0) return -1;
    environment_put(process,name,value);
    return 0;
}

int32_t process_environment_unset(const char *name){
    struct process *process=process_current();
    if(!process || !environment_name_valid(name)) return -1;
    int32_t slot=environment_find(process,name);
    if(slot<0) return -1;
    memset(&process->environment[slot],0,sizeof(process->environment[slot]));
    return 0;
}

int32_t process_environment_list(struct process_environment_entry *entries,
                                 uint32_t capacity){
    struct process *process=process_current();
    if(!process || (!entries && capacity)) return -1;
    uint32_t count=0;
    for(uint32_t index=0;index<PROCESS_ENVIRONMENT_COUNT;index++){
        if(!process->environment[index].used) continue;
        if(count<capacity) entries[count]=process->environment[index];
        count++;
    }
    return (int32_t)count;
}
