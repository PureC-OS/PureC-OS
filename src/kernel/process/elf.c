#include "elf.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../lib/string.h"

#define ELF_CLASS_64 2
#define ELF_DATA_LITTLE_ENDIAN 1
#define ELF_MACHINE_X86_64 62
#define ELF_TYPE_EXECUTABLE 2
#define ELF_PROGRAM_LOAD 1
#define ELF_FLAG_WRITABLE 2
#define ELF_FLAG_EXECUTABLE 1
#define ELF_USER_MIN 0x0000000000400000ULL
#define ELF_USER_MAX 0x0000700000000000ULL

struct elf64_header {
    uint8_t identity[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_offset;
    uint64_t section_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_entry_size;
    uint16_t program_count;
    uint16_t section_entry_size;
    uint16_t section_count;
    uint16_t section_names;
} __attribute__((packed));

struct elf64_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} __attribute__((packed));

static bool add_overflows(uint64_t left, uint64_t right){
    return left>UINT64_MAX-right;
}

static bool copy_to_space(uint64_t address_space, uint64_t destination,
                          const uint8_t *source, uint64_t size){
    while(size){
        uint64_t physical=vmm_translate(address_space,destination);
        if(!physical) return false;
        uint64_t amount=PMM_PAGE_SIZE-(destination&(PMM_PAGE_SIZE-1));
        if(amount>size) amount=size;
        memcpy(pmm_physical_to_virtual(physical),source,amount);
        destination+=amount;
        source+=amount;
        size-=amount;
    }
    return true;
}

bool elf_load_user_image(const void *image, uint64_t image_size,
                         uint64_t address_space,
                         struct elf_load_result *result){
    if(!image || !result || image_size<sizeof(struct elf64_header)) return false;
    const struct elf64_header *header=(const struct elf64_header*)image;
    if(header->identity[0]!=0x7F || header->identity[1]!='E'
       || header->identity[2]!='L' || header->identity[3]!='F'
       || header->identity[4]!=ELF_CLASS_64
       || header->identity[5]!=ELF_DATA_LITTLE_ENDIAN
       || header->type!=ELF_TYPE_EXECUTABLE
       || header->machine!=ELF_MACHINE_X86_64
       || header->program_entry_size!=sizeof(struct elf64_program_header)){
        return false;
    }
    uint64_t table_size=(uint64_t)header->program_count*header->program_entry_size;
    if(add_overflows(header->program_offset,table_size)
       || header->program_offset+table_size>image_size) return false;

    result->entry=header->entry;
    result->lowest_address=UINT64_MAX;
    result->highest_address=0;
    const uint8_t *bytes=(const uint8_t*)image;
    for(uint16_t index=0;index<header->program_count;index++){
        const struct elf64_program_header *program=
            (const struct elf64_program_header*)(bytes+header->program_offset
                +(uint64_t)index*header->program_entry_size);
        if(program->type!=ELF_PROGRAM_LOAD) continue;
        if(program->file_size>program->memory_size
           || add_overflows(program->offset,program->file_size)
           || program->offset+program->file_size>image_size
           || add_overflows(program->virtual_address,program->memory_size)
           || program->virtual_address<ELF_USER_MIN
           || program->virtual_address+program->memory_size>ELF_USER_MAX){
            return false;
        }
        uint64_t first=program->virtual_address&~(PMM_PAGE_SIZE-1);
        uint64_t end=(program->virtual_address+program->memory_size
                      +PMM_PAGE_SIZE-1)&~(PMM_PAGE_SIZE-1);
        uint64_t flags=VMM_PAGE_USER;
        if(program->flags&ELF_FLAG_WRITABLE) flags|=VMM_PAGE_WRITABLE;
        if(!(program->flags&ELF_FLAG_EXECUTABLE)) flags|=VMM_PAGE_NX;
        for(uint64_t address=first;address<end;address+=PMM_PAGE_SIZE){
            if(vmm_translate(address_space,address)) continue;
            uint64_t physical=pmm_allocate_page();
            if(!physical || !vmm_map_page(address_space,address,physical,flags)){
                if(physical) pmm_free_page(physical);
                return false;
            }
        }
        if(program->file_size
           && !copy_to_space(address_space,program->virtual_address,
                             bytes+program->offset,program->file_size)) return false;
        if(first<result->lowest_address) result->lowest_address=first;
        if(end>result->highest_address) result->highest_address=end;
    }
    if(result->lowest_address==UINT64_MAX
       || result->entry<result->lowest_address
       || result->entry>=result->highest_address) return false;
    return true;
}
