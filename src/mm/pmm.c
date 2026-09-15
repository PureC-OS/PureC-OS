#include "pmm.h"
#include "../kernel/diagnostics/klog.h"
#include "../lib/string.h"

#define PMM_MAX_PHYSICAL_BYTES (32ULL*1024ULL*1024ULL*1024ULL)
#define PMM_MAX_PAGES (PMM_MAX_PHYSICAL_BYTES/PMM_PAGE_SIZE)
#define PMM_BITMAP_BYTES (PMM_MAX_PAGES/8)

static uint8_t frame_bitmap[PMM_BITMAP_BYTES];
static uint64_t direct_map_offset;
static uint64_t frame_limit;
static uint64_t free_frames;
static uint64_t allocation_cursor;
static bool ready;

static void set_frame(uint64_t frame){
    frame_bitmap[frame>>3]|=(uint8_t)(1U<<(frame&7));
}

static void clear_frame(uint64_t frame){
    frame_bitmap[frame>>3]&=(uint8_t)~(1U<<(frame&7));
}

static bool frame_is_used(uint64_t frame){
    return (frame_bitmap[frame>>3]&(uint8_t)(1U<<(frame&7)))!=0;
}

void pmm_init(const struct limine_memmap_response *memory_map,
              uint64_t hhdm_offset){
    ready=false;
    direct_map_offset=hhdm_offset;
    frame_limit=0;
    free_frames=0;
    allocation_cursor=1;
    memset(frame_bitmap,0xFF,sizeof(frame_bitmap));
    if(!memory_map || !hhdm_offset) return;

    for(uint64_t index=0;index<memory_map->entry_count;index++){
        const struct limine_memmap_entry *entry=memory_map->entries[index];
        if(!entry || entry->type!=LIMINE_MEMMAP_USABLE) continue;
        if(entry->base>UINT64_MAX-entry->length) continue;
        if(entry->base>UINT64_MAX-(PMM_PAGE_SIZE-1)) continue;
        uint64_t begin=(entry->base+PMM_PAGE_SIZE-1)&~(PMM_PAGE_SIZE-1);
        uint64_t end=(entry->base+entry->length)&~(PMM_PAGE_SIZE-1);
        if(end>PMM_MAX_PHYSICAL_BYTES) end=PMM_MAX_PHYSICAL_BYTES;
        if(begin>=end) continue;
        uint64_t last_frame=end/PMM_PAGE_SIZE;
        if(last_frame>frame_limit) frame_limit=last_frame;
        for(uint64_t address=begin;address<end;address+=PMM_PAGE_SIZE){
            uint64_t frame=address/PMM_PAGE_SIZE;
            if(frame==0 || !frame_is_used(frame)) continue;
            clear_frame(frame);
            free_frames++;
        }
    }
    ready=free_frames!=0;
    klogf(ready ? KLOG_OK : KLOG_ERROR,
          "pmm: managed=%llu MiB free=%llu MiB page=%u",
          (frame_limit*PMM_PAGE_SIZE)/(1024*1024),
          (free_frames*PMM_PAGE_SIZE)/(1024*1024),(uint32_t)PMM_PAGE_SIZE);
}

uint64_t pmm_allocate_page(void){
    if(!ready || !free_frames) return 0;
    for(uint64_t pass=0;pass<2;pass++){
        uint64_t end=pass==0 ? frame_limit : allocation_cursor;
        uint64_t begin=pass==0 ? allocation_cursor : 1;
        for(uint64_t frame=begin;frame<end;frame++){
            if(frame_is_used(frame)) continue;
            set_frame(frame);
            free_frames--;
            allocation_cursor=frame+1;
            void *page=pmm_physical_to_virtual(frame*PMM_PAGE_SIZE);
            memset(page,0,PMM_PAGE_SIZE);
            return frame*PMM_PAGE_SIZE;
        }
    }
    return 0;
}

uint64_t pmm_allocate_contiguous(uint64_t page_count){
    if(!ready || !free_frames || !page_count) return 0;
    if(page_count==1) return pmm_allocate_page();
    if(page_count>free_frames) return 0;
    for(uint64_t pass=0;pass<2;pass++){
        uint64_t end=pass==0 ? frame_limit : allocation_cursor;
        uint64_t begin=pass==0 ? allocation_cursor : 1;
        if(end<begin+page_count) continue;
        for(uint64_t frame=begin;frame+page_count<=end;frame++){
            bool ok=true;
            for(uint64_t off=0;off<page_count;off++){
                if(frame_is_used(frame+off)){ ok=false; frame+=off; break; }
            }
            if(!ok) continue;
            for(uint64_t off=0;off<page_count;off++) set_frame(frame+off);
            free_frames-=page_count;
            allocation_cursor=frame+page_count;
            for(uint64_t off=0;off<page_count;off++){
                void *page=pmm_physical_to_virtual((frame+off)*PMM_PAGE_SIZE);
                memset(page,0,PMM_PAGE_SIZE);
            }
            return frame*PMM_PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free_contiguous(uint64_t physical_address, uint64_t page_count){
    if((physical_address&(PMM_PAGE_SIZE-1))!=0 || !page_count) return;
    uint64_t start_frame=physical_address/PMM_PAGE_SIZE;
    if(start_frame==0 || start_frame+page_count>frame_limit) return;
    for(uint64_t off=0;off<page_count;off++){
        uint64_t frame=start_frame+off;
        if(!frame_is_used(frame)) continue;
        clear_frame(frame);
        free_frames++;
        if(frame<allocation_cursor) allocation_cursor=frame;
    }
}

void pmm_free_page(uint64_t physical_address){
    if((physical_address&(PMM_PAGE_SIZE-1))!=0) return;
    uint64_t frame=physical_address/PMM_PAGE_SIZE;
    if(frame==0 || frame>=frame_limit || !frame_is_used(frame)) return;
    clear_frame(frame);
    free_frames++;
    if(frame<allocation_cursor) allocation_cursor=frame;
}

void *pmm_physical_to_virtual(uint64_t physical_address){
    return (void*)(uintptr_t)(physical_address+direct_map_offset);
}

uint64_t pmm_total_bytes(void){ return frame_limit*PMM_PAGE_SIZE; }
uint64_t pmm_free_bytes(void){ return free_frames*PMM_PAGE_SIZE; }
bool pmm_is_ready(void){ return ready; }
