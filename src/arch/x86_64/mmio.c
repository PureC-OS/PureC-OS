#include "mmio.h"
#include "../../kernel/sync/spinlock.h"
#include "../../kernel/smp/smp.h"
static spinlock_t mmio_lock=SPINLOCK_INIT;
#include <stddef.h>

#define PAGE_SIZE                 4096ULL
#define PAGE_ADDRESS_MASK         0x000FFFFFFFFFF000ULL
#define PAGE_PRESENT              (1ULL<<0)
#define PAGE_WRITABLE             (1ULL<<1)
#define PAGE_WRITE_THROUGH        (1ULL<<3)
#define PAGE_CACHE_DISABLE        (1ULL<<4)
#define PAGE_LARGE                (1ULL<<7)
#define MMIO_TABLE_POOL_PAGES     64
#define MMIO_FIRST_PML4_INDEX     508
#define MMIO_LAST_PML4_INDEX      384
#define MMIO_VIRTUAL_LIMIT        (1ULL<<39)
#define MMIO_REGION_LIMIT         16

struct mmio_region {
    uint64_t physical;
    uint64_t virtual_address;
    uint64_t size;
};

static uint64_t table_pool[MMIO_TABLE_POOL_PAGES][512]
    __attribute__((aligned(PAGE_SIZE)));
static uint32_t table_pool_used;
static uint64_t direct_map_offset;
static uint64_t kernel_phys_base;
static uint64_t kernel_virt_base;
static uint64_t mmio_virtual_base;
static uint64_t mmio_virtual_cursor;
static struct mmio_region regions[MMIO_REGION_LIMIT];
static uint8_t region_count;
static uint8_t physical_address_bits;
static bool configured;
static bool ready;

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx){
    __asm__ volatile("cpuid"
                     : "=a"(*eax),"=b"(*ebx),"=c"(*ecx),"=d"(*edx)
                     : "a"(leaf),"c"(0));
}

static uint64_t pointer_physical(const void *pointer){
    return (uint64_t)(uintptr_t)pointer-kernel_virt_base+kernel_phys_base;
}

static uint64_t *physical_pointer(uint64_t physical){
    return (uint64_t*)(uintptr_t)(direct_map_offset+physical);
}

static uint64_t canonical_pml4_base(uint16_t index){
    uint64_t address=(uint64_t)index<<39;
    if(index&0x100) address|=0xFFFF000000000000ULL;
    return address;
}

static uint64_t *allocate_table(void){
    if(table_pool_used>=MMIO_TABLE_POOL_PAGES) return NULL;
    uint64_t *table=table_pool[table_pool_used++];
    for(uint16_t index=0;index<512;index++) table[index]=0;
    return table;
}

static uint64_t *next_table(uint64_t *table, uint16_t index){
    uint64_t entry=table[index];
    if(entry&PAGE_PRESENT){
        if(entry&PAGE_LARGE) return NULL;
        return physical_pointer(entry&PAGE_ADDRESS_MASK);
    }
    uint64_t *next=allocate_table();
    if(!next) return NULL;
    table[index]=pointer_physical(next)|PAGE_PRESENT|PAGE_WRITABLE;
    return next;
}

static bool map_page(uint64_t virtual_address, uint64_t physical_address){
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4=physical_pointer(cr3&PAGE_ADDRESS_MASK);
    uint16_t pml4_index=(uint16_t)((virtual_address>>39)&0x1FF);
    uint16_t pdpt_index=(uint16_t)((virtual_address>>30)&0x1FF);
    uint16_t pd_index=(uint16_t)((virtual_address>>21)&0x1FF);
    uint16_t pt_index=(uint16_t)((virtual_address>>12)&0x1FF);

    uint64_t *pdpt=next_table(pml4,pml4_index);
    if(!pdpt) return false;
    uint64_t *pd=next_table(pdpt,pdpt_index);
    if(!pd) return false;
    uint64_t *pt=next_table(pd,pd_index);
    if(!pt) return false;
    pt[pt_index]=(physical_address&PAGE_ADDRESS_MASK)
        |PAGE_PRESENT|PAGE_WRITABLE|PAGE_WRITE_THROUGH|PAGE_CACHE_DISABLE;
    __asm__ volatile("invlpg (%0)"::"r"(virtual_address):"memory");
    return true;
}

void mmio_configure(uint64_t hhdm_offset,
                    uint64_t kernel_physical_base,
                    uint64_t kernel_virtual_base){
    ready=false;
    direct_map_offset=hhdm_offset;
    kernel_phys_base=kernel_physical_base;
    kernel_virt_base=kernel_virtual_base;
    configured=true;
}

void mmio_init(void){
    ready=false;
    if(!configured) return;
    table_pool_used=0;
    region_count=0;
    physical_address_bits=36;
    uint32_t eax,ebx,ecx,edx;
    cpuid(0x80000000U,&eax,&ebx,&ecx,&edx);
    if(eax>=0x80000008U){
        cpuid(0x80000008U,&eax,&ebx,&ecx,&edx);
        uint8_t reported=(uint8_t)eax;
        if(reported>=32 && reported<=52) physical_address_bits=reported;
    }

    uint64_t cr3,cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if(cr4&(1ULL<<12)) return;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4=physical_pointer(cr3&PAGE_ADDRESS_MASK);
    for(uint16_t index=MMIO_FIRST_PML4_INDEX;index>=MMIO_LAST_PML4_INDEX;index--){
        if(!(pml4[index]&PAGE_PRESENT)){
            mmio_virtual_base=canonical_pml4_base(index);
            mmio_virtual_cursor=0;
            ready=true;
            return;
        }
    }
}

bool mmio_is_ready(void){
    return ready;
}

static volatile void *mmio_map_locked(uint64_t physical_address, uint64_t size){
    if(!ready || !physical_address || !size) return NULL;
    if(physical_address+size<physical_address) return NULL;

    uint64_t requested_end=physical_address+size;
    uint64_t physical_limit=(1ULL<<physical_address_bits)-1;
    if(physical_address>physical_limit || requested_end-1>physical_limit) return NULL;
    for(uint8_t index=0;index<region_count;index++){
        uint64_t region_end=regions[index].physical+regions[index].size;
        if(physical_address>=regions[index].physical && requested_end<=region_end){
            uint64_t offset=physical_address-regions[index].physical;
            return (volatile void*)(uintptr_t)(regions[index].virtual_address+offset);
        }
    }
    if(region_count>=MMIO_REGION_LIMIT) return NULL;

    uint64_t page_offset=physical_address&(PAGE_SIZE-1);
    uint64_t physical_base=physical_address&~(PAGE_SIZE-1);
    uint64_t span=page_offset+size;
    if(span<page_offset) return NULL;
    uint64_t page_count=(span+PAGE_SIZE-1)/PAGE_SIZE;
    uint64_t mapping_size=page_count*PAGE_SIZE;
    if(mapping_size<page_count || mmio_virtual_cursor>MMIO_VIRTUAL_LIMIT-mapping_size)
        return NULL;

    uint64_t virtual_base=mmio_virtual_base+mmio_virtual_cursor;
    for(uint64_t page=0;page<page_count;page++){
        if(!map_page(virtual_base+page*PAGE_SIZE,
                     physical_base+page*PAGE_SIZE)) return NULL;
    }
    mmio_virtual_cursor+=mapping_size;
    regions[region_count].physical=physical_base;
    regions[region_count].virtual_address=virtual_base;
    regions[region_count].size=mapping_size;
    region_count++;
    return (volatile void*)(uintptr_t)(virtual_base+page_offset);
}

volatile void *mmio_map(uint64_t physical_address,uint64_t size){
    uint64_t flags=spin_lock_irqsave(&mmio_lock);
    volatile void *result=mmio_map_locked(physical_address,size);
    smp_tlb_shootdown();
    spin_unlock_irqrestore(&mmio_lock,flags);
    return result;
}
