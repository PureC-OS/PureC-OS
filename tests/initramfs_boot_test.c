#include "fs/initramfs.h"
#include "boot/install_source.h"
#include "kernel/diagnostics/klog.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char archive[512];
static uint32_t archive_size;
static const char *loaded_path;

bool boot_get_module(const char *path, const void **data, uint64_t *size){
    if(!loaded_path || strcmp(path,loaded_path)!=0) return false;
    *data=archive;
    *size=archive_size;
    return true;
}

void klog(enum klog_level level, const char *message){
    (void)level; (void)message;
}

void klogf(enum klog_level level, const char *format, ...){
    (void)level; (void)format;
}

static void add_entry(const char *name, const void *data, uint32_t size){
    char header[111];
    uint32_t namesize=(uint32_t)strlen(name)+1;
    int length=snprintf(header,sizeof(header),
        "070701%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
        1u,0100644u,0u,0u,1u,0u,size,0u,0u,0u,0u,namesize,0u);
    assert(length==110);
    assert(archive_size+110+namesize+size+6<=sizeof(archive));
    memcpy(archive+archive_size,header,110);
    archive_size+=110;
    memcpy(archive+archive_size,name,namesize);
    archive_size=(archive_size+namesize+3)&~3u;
    if(size) memcpy(archive+archive_size,data,size);
    archive_size=(archive_size+size+3)&~3u;
}

int main(void){
    static const char payload[]="init image";
    static const char *paths[]={
        "/boot/initramfs.cpio",
        "/boot/initra~1.cpi", 
        "/boot/INITRA~1.CPI"
    };
    add_entry("bin/init",payload,sizeof(payload));
    add_entry("TRAILER!!!",NULL,0);
    for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);i++){
        loaded_path=paths[i];
        assert(initramfs_mount());
        const void *data=NULL;
        uint32_t size=0;
        assert(initramfs_find("/bin/init",&data,&size));
        assert(size==sizeof(payload));
        assert(memcmp(data,payload,size)==0);
        assert(!initramfs_find("/bin/missing",&data,&size));
        uint64_t boot_size=0;
        assert(initramfs_boot_image(&data,&boot_size));
        assert(data==archive && boot_size==archive_size);
    }
    loaded_path="/boot/unrelated.cpio";
    const void *data=NULL;
    uint64_t size=0;
    assert(!initramfs_boot_image(&data,&size));
    assert(!initramfs_boot_image(NULL,&size));
    assert(!initramfs_boot_image(&data,NULL));
    puts("Initramfs boot path tests passed");
    return 0;
}
