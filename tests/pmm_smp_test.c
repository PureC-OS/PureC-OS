#include "mm/pmm.h"
#include "boot/limine.h"
#include "kernel/diagnostics/klog.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGES 512
static _Atomic unsigned owners[PAGES+1];
void klogf(enum klog_level level,const char *format,...){ (void)level; (void)format; }
static void *worker(void *arg){
    unsigned tag=(unsigned)(uintptr_t)arg;
    for(unsigned n=0;n<2000;n++){
        unsigned count=n%3+1;
        uint64_t physical=pmm_allocate_contiguous(count);
        assert(physical);
        unsigned first=(unsigned)(physical/PMM_PAGE_SIZE);
        for(unsigned i=0;i<count;i++){
            assert(!atomic_exchange(&owners[first+i],tag));
            unsigned char *page=pmm_physical_to_virtual(physical+i*PMM_PAGE_SIZE);
            for(unsigned j=0;j<PMM_PAGE_SIZE;j++) assert(page[j]==0);
            memset(page,(int)tag,PMM_PAGE_SIZE);
        }
        for(unsigned i=0;i<count;i++){
            unsigned char *page=pmm_physical_to_virtual(physical+i*PMM_PAGE_SIZE);
            for(unsigned j=0;j<PMM_PAGE_SIZE;j++) assert(page[j]==tag);
            assert(atomic_exchange(&owners[first+i],0)==tag);
        }
        pmm_free_contiguous(physical,count);
    }
    return NULL;
}
int main(void){
    void *pool=aligned_alloc(PMM_PAGE_SIZE,PAGES*PMM_PAGE_SIZE);
    assert(pool);
    struct limine_memmap_entry entry={.base=PMM_PAGE_SIZE,.length=PAGES*PMM_PAGE_SIZE,.type=LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry *entries[]={&entry};
    struct limine_memmap_response map={.entry_count=1,.entries=entries};
    pmm_init(&map,(uint64_t)(uintptr_t)pool-PMM_PAGE_SIZE);
    uint64_t initial=pmm_free_bytes();
    pthread_t workers[8];
    for(unsigned i=0;i<8;i++) assert(!pthread_create(&workers[i],NULL,worker,(void*)(uintptr_t)(i+1)));
    for(unsigned i=0;i<8;i++) assert(!pthread_join(workers[i],NULL));
    assert(pmm_free_bytes()==initial);
    pmm_free_contiguous(PMM_PAGE_SIZE,UINT64_MAX);
    assert(pmm_free_bytes()==initial);
    free(pool);
    puts("PMM: 16000 concurrent allocations, no aliasing or leaks");
}
