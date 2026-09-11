#include "system_info.h"
#include "../../drivers/interrupts/timer.h"
#include "../process/scheduler.h"

#include <stddef.h>

#define CPU_NAME_CAPACITY 49

static char cpu_name[CPU_NAME_CAPACITY] = "Unknown x86_64 CPU";
static uint64_t usable_ram_bytes;
static uint64_t total_ram_bytes;
static uint64_t tsc_frequency_hz=3000000000;
static uint64_t boot_tsc;
static uint64_t cpu_sample_ticks;
static uint64_t cpu_sample_idle_ticks;
static uint32_t logical_processors=1;

static uint64_t read_tsc(void){
    uint32_t low,high;
    __asm__ volatile("rdtsc":"=a"(low),"=d"(high));
    return ((uint64_t)high<<32)|low;
}

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx){
    __asm__ volatile(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0)
    );
}

static void copy_register(char *destination, uint32_t value){
    for(uint32_t byte=0;byte<4;byte++){
        destination[byte]=(char)(value>>(byte*8));
    }
}

static void remove_leading_spaces(char *text){
    size_t start=0;
    while(text[start]==' ') start++;
    if(start==0) return;

    size_t destination=0;
    do {
        text[destination++]=text[start++];
    } while(text[start-1]);
}

static void detect_cpu_name(void){
    uint32_t eax,ebx,ecx,edx;
    cpuid(0x80000000,&eax,&ebx,&ecx,&edx);

    if(eax>=0x80000004){
        for(uint32_t part=0;part<3;part++){
            cpuid(0x80000002+part,&eax,&ebx,&ecx,&edx);
            char *destination=&cpu_name[part*16];
            copy_register(destination,eax);
            copy_register(destination+4,ebx);
            copy_register(destination+8,ecx);
            copy_register(destination+12,edx);
        }
        cpu_name[CPU_NAME_CAPACITY-1]='\0';
        remove_leading_spaces(cpu_name);
        return;
    }

    cpuid(0,&eax,&ebx,&ecx,&edx);
    copy_register(cpu_name,ebx);
    copy_register(cpu_name+4,edx);
    copy_register(cpu_name+8,ecx);
    cpu_name[12]='\0';
}

static void detect_logical_processors(void){
    uint32_t eax,ebx,ecx,edx;
    cpuid(0,&eax,&ebx,&ecx,&edx);
    if(eax<1) return;
    cpuid(1,&eax,&ebx,&ecx,&edx);
    uint32_t detected=(ebx>>16)&0xFF;
    if(detected) logical_processors=detected;
}

static void detect_tsc_frequency(void){
    uint32_t eax,ebx,ecx,edx;
    cpuid(0,&eax,&ebx,&ecx,&edx);
    uint32_t maximum_leaf=eax;

    if(maximum_leaf>=0x15){
        cpuid(0x15,&eax,&ebx,&ecx,&edx);
        if(eax && ebx && ecx){
            tsc_frequency_hz=((uint64_t)ecx*ebx)/eax;
            return;
        }
    }
    if(maximum_leaf>=0x16){
        cpuid(0x16,&eax,&ebx,&ecx,&edx);
        if(eax) tsc_frequency_hz=(uint64_t)eax*1000000;
    }
}

static void detect_usable_ram(const struct limine_memmap_response *memory_map){
    usable_ram_bytes=0;
    total_ram_bytes=0;
    if(!memory_map) return;

    for(uint64_t index=0;index<memory_map->entry_count;index++){
        const struct limine_memmap_entry *entry=memory_map->entries[index];
        if(entry && entry->type==LIMINE_MEMMAP_USABLE){
            usable_ram_bytes+=entry->length;
        }
        if(entry && (entry->type==LIMINE_MEMMAP_USABLE
                     || entry->type==LIMINE_MEMMAP_ACPI_RECLAIMABLE
                     || entry->type==LIMINE_MEMMAP_ACPI_NVS
                     || entry->type==LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE
                     || entry->type==LIMINE_MEMMAP_KERNEL_AND_MODULES
                     || entry->type==LIMINE_MEMMAP_FRAMEBUFFER)){
            total_ram_bytes+=entry->length;
        }
    }
}

void system_info_init(const struct limine_memmap_response *memory_map){
    boot_tsc=read_tsc();
    detect_cpu_name();
    detect_logical_processors();
    detect_tsc_frequency();
    detect_usable_ram(memory_map);
    cpu_sample_ticks=0;
    cpu_sample_idle_ticks=0;
}

const char *system_info_cpu_name(void){ return cpu_name; }

uint64_t system_info_usable_ram_bytes(void){ return usable_ram_bytes; }

uint64_t system_info_total_ram_bytes(void){ return total_ram_bytes; }

uint64_t system_info_tsc_frequency_hz(void){ return tsc_frequency_hz; }

uint32_t system_info_logical_processors(void){ return logical_processors; }

uint64_t system_info_uptime_ms(void){
    if(!tsc_frequency_hz) return 0;
    return ((read_tsc()-boot_tsc)*1000)/tsc_frequency_hz;
}

uint32_t system_info_cpu_usage_percent(void){
    uint64_t now=scheduler_total_ticks();
    uint64_t idle=scheduler_idle_ticks();
    uint64_t elapsed=now-cpu_sample_ticks;
    uint64_t idle_elapsed=idle-cpu_sample_idle_ticks;
    cpu_sample_ticks=now;
    cpu_sample_idle_ticks=idle;
    if(!elapsed) return 0;
    if(idle_elapsed>elapsed) idle_elapsed=elapsed;
    return 100U-(uint32_t)((idle_elapsed*100)/elapsed);
}
