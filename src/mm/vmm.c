#include "vmm.h"
#include "pmm.h"
#include "../kernel/diagnostics/klog.h"
#include "../lib/string.h"

#define PAGE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL
#define USER_TOP 0x0000800000000000ULL

static uint64_t kernel_address_space;
static bool nx_enabled;

static void enable_nx(void){
    uint32_t maximum,eax,ebx,ecx,edx;
    __asm__ volatile("cpuid":"=a"(maximum),"=b"(ebx),"=c"(ecx),"=d"(edx)
                     :"a"(0x80000000U),"c"(0));
    if(maximum<0x80000001U) return;
    __asm__ volatile("cpuid":"=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx)
                     :"a"(0x80000001U),"c"(0));
    if(!(edx&(1U<<20))) return;
    uint32_t low,high;
    __asm__ volatile("rdmsr":"=a"(low),"=d"(high):"c"(0xC0000080U));
    low|=1U<<11;
    __asm__ volatile("wrmsr"::"a"(low),"d"(high),"c"(0xC0000080U));
    nx_enabled=true;
}

static uint64_t read_cr3(void){
    uint64_t value;
    __asm__ volatile("mov %%cr3,%0":"=r"(value));
    return value&PAGE_ADDRESS_MASK;
}

static uint64_t *table_from_entry(uint64_t entry){
    return (uint64_t*)pmm_physical_to_virtual(entry&PAGE_ADDRESS_MASK);
}

static uint64_t *next_table(uint64_t *table, uint16_t index, bool create,
                            uint64_t flags){
    if(table[index]&VMM_PAGE_PRESENT) return table_from_entry(table[index]);
    if(!create) return 0;
    uint64_t physical=pmm_allocate_page();
    if(!physical) return 0;
    table[index]=physical|VMM_PAGE_PRESENT|VMM_PAGE_WRITABLE
        |(flags&VMM_PAGE_USER);
    return (uint64_t*)pmm_physical_to_virtual(physical);
}

void vmm_init(void){
    kernel_address_space=read_cr3();
    enable_nx();
    klogf(KLOG_OK,"vmm: kernel cr3=0x%llx nx=%s",kernel_address_space,
          nx_enabled ? "on" : "unsupported");
}

uint64_t vmm_kernel_address_space(void){ return kernel_address_space; }

uint64_t vmm_create_address_space(void){
    uint64_t physical=pmm_allocate_page();
    if(!physical) return 0;
    uint64_t *destination=(uint64_t*)pmm_physical_to_virtual(physical);
    uint64_t *kernel=(uint64_t*)pmm_physical_to_virtual(kernel_address_space);
    for(uint16_t index=256;index<512;index++) destination[index]=kernel[index];
    return physical;
}

void vmm_destroy_address_space(uint64_t address_space){
    if(!address_space || address_space==kernel_address_space) return;
    uint64_t *pml4=(uint64_t*)pmm_physical_to_virtual(address_space);
    for(uint16_t pml4_index=0;pml4_index<256;pml4_index++){
        if(!(pml4[pml4_index]&VMM_PAGE_PRESENT)) continue;
        uint64_t pdpt_physical=pml4[pml4_index]&PAGE_ADDRESS_MASK;
        uint64_t *pdpt=table_from_entry(pml4[pml4_index]);
        for(uint16_t pdpt_index=0;pdpt_index<512;pdpt_index++){
            if(!(pdpt[pdpt_index]&VMM_PAGE_PRESENT)) continue;
            uint64_t pd_physical=pdpt[pdpt_index]&PAGE_ADDRESS_MASK;
            uint64_t *pd=table_from_entry(pdpt[pdpt_index]);
            for(uint16_t pd_index=0;pd_index<512;pd_index++){
                if(!(pd[pd_index]&VMM_PAGE_PRESENT)) continue;
                uint64_t pt_physical=pd[pd_index]&PAGE_ADDRESS_MASK;
                uint64_t *pt=table_from_entry(pd[pd_index]);
                for(uint16_t pt_index=0;pt_index<512;pt_index++){
                    if(pt[pt_index]&VMM_PAGE_PRESENT)
                        pmm_free_page(pt[pt_index]&PAGE_ADDRESS_MASK);
                }
                pmm_free_page(pt_physical);
            }
            pmm_free_page(pd_physical);
        }
        pmm_free_page(pdpt_physical);
    }
    pmm_free_page(address_space);
}

bool vmm_map_page(uint64_t address_space, uint64_t virtual_address,
                  uint64_t physical_address, uint64_t flags){
    if((virtual_address&(PMM_PAGE_SIZE-1))
       || (physical_address&(PMM_PAGE_SIZE-1))) return false;
    uint64_t *pml4=(uint64_t*)pmm_physical_to_virtual(address_space);
    uint64_t *pdpt=next_table(pml4,(virtual_address>>39)&0x1FF,true,flags);
    if(!pdpt) return false;
    uint64_t *pd=next_table(pdpt,(virtual_address>>30)&0x1FF,true,flags);
    if(!pd) return false;
    uint64_t *pt=next_table(pd,(virtual_address>>21)&0x1FF,true,flags);
    if(!pt) return false;
    uint16_t index=(virtual_address>>12)&0x1FF;
    if(pt[index]&VMM_PAGE_PRESENT) return false;
    pt[index]=(physical_address&PAGE_ADDRESS_MASK)|VMM_PAGE_PRESENT
        |(flags&(VMM_PAGE_WRITABLE|VMM_PAGE_USER))
        |(nx_enabled ? flags&VMM_PAGE_NX : 0);
    return true;
}

bool vmm_map_new_pages(uint64_t address_space, uint64_t virtual_address,
                       uint64_t page_count, uint64_t flags){
    for(uint64_t page=0;page<page_count;page++){
        uint64_t physical=pmm_allocate_page();
        if(!physical) return false;
        if(!vmm_map_page(address_space,virtual_address+page*PMM_PAGE_SIZE,
                         physical,flags)){
            pmm_free_page(physical);
            return false;
        }
    }
    return true;
}

uint64_t vmm_translate(uint64_t address_space, uint64_t virtual_address){
    uint64_t *table=(uint64_t*)pmm_physical_to_virtual(address_space);
    uint16_t indices[4]={
        (uint16_t)((virtual_address>>39)&0x1FF),
        (uint16_t)((virtual_address>>30)&0x1FF),
        (uint16_t)((virtual_address>>21)&0x1FF),
        (uint16_t)((virtual_address>>12)&0x1FF)
    };
    for(uint8_t level=0;level<3;level++){
        uint64_t entry=table[indices[level]];
        if(!(entry&VMM_PAGE_PRESENT)) return 0;
        table=table_from_entry(entry);
    }
    uint64_t entry=table[indices[3]];
    if(!(entry&VMM_PAGE_PRESENT)) return 0;
    return (entry&PAGE_ADDRESS_MASK)|(virtual_address&(PMM_PAGE_SIZE-1));
}

bool vmm_user_range_accessible(uint64_t address_space, uint64_t address,
                               uint64_t size, bool writable){
    if(!size) return true;
    if(address>=USER_TOP || size>USER_TOP-address) return false;
    uint64_t first=address&~(PMM_PAGE_SIZE-1);
    uint64_t last=(address+size-1)&~(PMM_PAGE_SIZE-1);
    uint64_t *pml4=(uint64_t*)pmm_physical_to_virtual(address_space);
    for(uint64_t page=first;;page+=PMM_PAGE_SIZE){
        uint64_t *pdpt=next_table(pml4,(page>>39)&0x1FF,false,0);
        if(!pdpt) return false;
        uint64_t *pd=next_table(pdpt,(page>>30)&0x1FF,false,0);
        if(!pd) return false;
        uint64_t *pt=next_table(pd,(page>>21)&0x1FF,false,0);
        if(!pt) return false;
        uint64_t entry=pt[(page>>12)&0x1FF];
        if(!(entry&VMM_PAGE_PRESENT) || !(entry&VMM_PAGE_USER)
           || (writable && !(entry&VMM_PAGE_WRITABLE))) return false;
        if(page==last) break;
    }
    return true;
}

uint64_t vmm_user_page_count(uint64_t address_space){
    if(!address_space || address_space==kernel_address_space) return 0;
    uint64_t count=0;
    uint64_t *pml4=(uint64_t*)pmm_physical_to_virtual(address_space);
    for(uint16_t pml4_index=0;pml4_index<256;pml4_index++){
        if(!(pml4[pml4_index]&VMM_PAGE_PRESENT)) continue;
        uint64_t *pdpt=table_from_entry(pml4[pml4_index]);
        for(uint16_t pdpt_index=0;pdpt_index<512;pdpt_index++){
            if(!(pdpt[pdpt_index]&VMM_PAGE_PRESENT)) continue;
            uint64_t *pd=table_from_entry(pdpt[pdpt_index]);
            for(uint16_t pd_index=0;pd_index<512;pd_index++){
                if(!(pd[pd_index]&VMM_PAGE_PRESENT)) continue;
                uint64_t *pt=table_from_entry(pd[pd_index]);
                for(uint16_t pt_index=0;pt_index<512;pt_index++)
                    if((pt[pt_index]&(VMM_PAGE_PRESENT|VMM_PAGE_USER))
                       ==(VMM_PAGE_PRESENT|VMM_PAGE_USER)) count++;
            }
        }
    }
    return count;
}

void vmm_switch_address_space(uint64_t address_space){
    if(!address_space || read_cr3()==address_space) return;
    __asm__ volatile("mov %0,%%cr3"::"r"(address_space):"memory");
}
