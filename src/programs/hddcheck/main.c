#include "../../libc/include/purec.h"

#define HDDCHECK_DEVICE_CAPACITY 20

int hddcheck_main(void){
    struct storage_device_info devices[HDDCHECK_DEVICE_CAPACITY];
    int32_t count=pc_list_disks(devices,HDDCHECK_DEVICE_CAPACITY);
    if(count<0){
        pc_write("hddcheck: enumeration failed\n");
        return 1;
    }
    struct disk_monitor_info stats={0};
    if(pc_syscall(SYS_DISK_STATS,(uint64_t)(uintptr_t)&stats,0,0)<0){
        stats.device_count=(uint32_t)count;
    }
    char root[32];
    root[0]='\0';
    if(pc_get_root_device(root,sizeof(root))<0) root[0]='\0';
    char fstype[16];
    fstype[0]='\0';
    if(pc_get_fs_type(fstype,sizeof(fstype))<0) fstype[0]='\0';
    pc_write("Disks: ");
    pc_write_u64((uint64_t)count);
    pc_write(" (operational ");
    pc_write_u64(stats.operational_count);
    pc_write(")\nTotal capacity: ");
    pc_write_u64(stats.total_bytes/(1024u*1024u*1024u));
    pc_write(" GiB\nRoot: ");
    pc_write(root[0] ? root : "unknown");
    pc_write("  FS: ");
    pc_write(fstype[0] ? fstype : "unknown");
    pc_write("\n");
    if(!count){
        pc_write("Status: FAIL (no block devices)\n");
        return 0;
    }
    for(int32_t i=0;i<count;i++){
        uint64_t mib=devices[i].sector_count*devices[i].sector_size/(1024u*1024u);
        pc_write("  ");
        pc_write(devices[i].name);
        pc_write("  ");
        pc_write_u64(mib);
        pc_write(" MiB  ");
        pc_write(devices[i].model[0] ? devices[i].model : "disk");
        pc_write(devices[i].operational ? "  online" : "  OFFLINE");
        pc_write(devices[i].writable ? "  rw" : "  ro");
        if(root[0] && pc_strcmp(devices[i].name,root)==0) pc_write("  [root]");
        pc_write("\n");
    }
    struct ext2_super_info sb={0};
    if(pc_ext2_super(&sb)>=0 && sb.magic==0xEF53){
        uint64_t block=(uint64_t)sb.block_size;
        uint64_t total_b=(uint64_t)sb.total_blocks*block;
        uint64_t free_b=(uint64_t)sb.free_blocks*block;
        pc_write("ext2: blocks ");
        pc_write_u64(sb.total_blocks);
        pc_write(" free ");
        pc_write_u64(sb.free_blocks);
        pc_write(" (");
        pc_write_u64(total_b/(1024u*1024u));
        pc_write(" MiB / ");
        pc_write_u64(free_b/(1024u*1024u));
        pc_write(" MiB free) inodes ");
        pc_write_u64(sb.free_inodes);
        pc_write("/");
        pc_write_u64(sb.total_inodes);
        pc_write(" free\n");
    }
    bool failed=false;
    for(int32_t i=0;i<count;i++) if(!devices[i].operational) failed=true;
    pc_write("Status: ");
    pc_write(failed ? "WARN (offline device present)\n" : "PASS\n");
    return 0;
}

void _start(void){
    pc_exit(hddcheck_main());
}
