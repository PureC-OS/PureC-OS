#include "filesystem.h"
#include "../terminal/path.h"
#include "../../libc/include/purec.h"
#include "../../libfs/include/purefs.h"

#define SYSTEM_PATH_CAPACITY 128

static bool resolve_path(const char *input, const char *fallback,
                         char output[SYSTEM_PATH_CAPACITY]){
    char current[SYSTEM_PATH_CAPACITY];
    if(!input[0]) input=fallback;
    if(pc_getenv("PWD",current,sizeof(current))<0)
        pc_copy(current,"/",sizeof(current));
    return shell_path_normalize(current,input,output,SYSTEM_PATH_CAPACITY);
}

static int command_ls(const char *arguments){
    char path[SYSTEM_PATH_CAPACITY];
    if(!resolve_path(arguments,".",path)){
        pc_write("ls: invalid path\n");
        return 1;
    }
    struct pf_entry entries[32];
    int32_t count=pf_list(path,entries,32);
    if(count<0){
        pc_write("ls: cannot list directory, error ");
        pc_write_i64(count);
        pc_write("\n");
        return 1;
    }
    if(!count){
        pc_write("(empty)\n");
        return 0;
    }
    for(int32_t index=0;index<count;index++){
        pc_write(pf_is_dir(&entries[index])
            ? "[DIR]  " : "[FILE] ");
        pc_write(entries[index].name);
        if(!pf_is_dir(&entries[index])){
            pc_write("  ");
            pc_write_u64(entries[index].size);
            pc_write(" bytes");
        }
        pc_write("\n");
    }
    return 0;
}

static int print_file(const char *path, bool add_final_newline){
    int32_t descriptor=pf_open(path);
    if(descriptor<0){
        pc_write("cat: cannot open file, error ");
        pc_write_i64(descriptor);
        pc_write("\n");
        return 1;
    }
    bool wrote=false;
    char last='\0';
    for(;;){
        char buffer[256];
        int32_t count=pf_read(descriptor,buffer,sizeof(buffer));
        if(count<0){
            (void)pf_close(descriptor);
            pc_write("cat: read failed\n");
            return 1;
        }
        if(!count) break;
        for(int32_t index=0;index<count;index++){
            char text[2]={buffer[index],'\0'};
            pc_write(text);
            last=buffer[index];
        }
        wrote=true;
    }
    (void)pf_close(descriptor);
    if(add_final_newline && wrote && last!='\n') pc_write("\n");
    return 0;
}

static int command_cat(const char *arguments){
    char path[SYSTEM_PATH_CAPACITY];
    if(!arguments[0] || !resolve_path(arguments,"",path)){
        pc_write("cat: file path required\n");
        return 1;
    }
    return print_file(path,true);
}

static int command_create(const char *name, const char *arguments,
                          bool is_dir){
    char path[SYSTEM_PATH_CAPACITY];
    if(!arguments[0] || !resolve_path(arguments,"",path)){
        pc_write(name);
        pc_write(": path required\n");
        return 1;
    }
    int64_t status=is_dir ? pf_create_dir(path) : pf_create_file(path);
    if(status<0){
        pc_write(name);
        pc_write(": cannot create path, error ");
        pc_write_i64(status);
        pc_write("\n");
        return 1;
    }
    return 0;
}

static int command_dmesg(void){
    pc_write("--- kernel log ---\n");
    int status=print_file("/kernel.log",false);
    pc_write("\n--- end kernel log ---\n");
    return status;
}

static int command_savelog(void){
    int32_t source=pf_open("/kernel.log");
    if(source<0){
        pc_write("savelog: /kernel.log is unavailable\n");
        return 1;
    }
    if(pf_write_file("/dmesg.txt",0,0)<0){
        (void)pf_close(source);
        pc_write("savelog: cannot create /dmesg.txt\n");
        return 1;
    }
    uint64_t total=0;
    for(;;){
        char buffer[512];
        int32_t count=pf_read(source,buffer,sizeof(buffer));
        if(count<0){
            (void)pf_close(source);
            return 1;
        }
        if(!count) break;
        int64_t written=pf_append_file("/dmesg.txt",buffer,(uint32_t)count);
        if(written!=count){
            (void)pf_close(source);
            pc_write("savelog: write failed\n");
            return 1;
        }
        total+=(uint32_t)written;
    }
    (void)pf_close(source);
    pc_write("Saved ");
    pc_write_u64(total);
    pc_write(" bytes to /dmesg.txt\n");
    return 0;
}

static bool parse_u32_simple(const char *s, uint32_t *out){
    if(!s||!*s) return false;
    uint32_t v=0;
    for(;*s;s++){
        if(*s<'0'||*s>'9') return false;
        uint32_t d=(uint32_t)(*s-'0');
        if(v> (UINT32_MAX-d)/10) return false;
        v=v*10+d;
    }
    *out=v; return true;
}
static void print_mode_ext2(uint16_t mode){
    pc_write("0x");
    const char *hex="0123456789ABCDEF";
    char buf[5]; buf[0]=hex[(mode>>12)&0xF]; buf[1]=hex[(mode>>8)&0xF]; buf[2]=hex[(mode>>4)&0xF]; buf[3]=hex[mode&0xF]; buf[4]=0;
    pc_write(buf);
    pc_write(" ("); if((mode&0xF000)==0x4000) pc_write("dir"); else if((mode&0xF000)==0x8000) pc_write("file"); else pc_write("other"); pc_write(")");
}
static void dump_stat_ext2(const struct ext2_stat_info *st){
    pc_write("inode "); pc_write_u64(st->ino); pc_write("\n");
    pc_write(" mode: "); print_mode_ext2(st->mode); pc_write("\n");
    pc_write(" links: "); pc_write_u64(st->links); pc_write("\n");
    pc_write(" size: "); pc_write_u64(st->size); pc_write(" bytes\n");
    pc_write(" blocks: "); pc_write_u64(st->blocks); pc_write(" sectors\n");
    pc_write(" uid "); pc_write_u64(st->uid); pc_write(" gid "); pc_write_u64(st->gid); pc_write("\n");
    pc_write(" direct:");
    for(int i=0;i<12;i++){ pc_write(" "); pc_write_u64(st->blocks_ptr[i]); }
    pc_write("\n single: "); pc_write_u64(st->blocks_ptr[12]); pc_write(" double: "); pc_write_u64(st->blocks_ptr[13]); pc_write(" triple: "); pc_write_u64(st->blocks_ptr[14]); pc_write("\n");
}
static int command_stat(const char *arguments){
    char path[SYSTEM_PATH_CAPACITY];
    if(!resolve_path(arguments,"",path)){ pc_write("stat: path required\n"); return 1; }
    struct ext2_stat_info st;
    int32_t r=pc_ext2_stat(path,&st);
    if(r<0){ pc_write("stat: error "); pc_write_i64(r); pc_write(r==-8?" (need ext2)\n":"\n"); return 1; }
    pc_write("path: "); pc_write(path); pc_write("\n");
    dump_stat_ext2(&st);
    return 0;
}
static int command_inode(const char *arguments){
    const char *s=arguments;
    while(*s==' '||*s=='\t') s++;
    if(!*s){ pc_write("inode: number required\n"); return 1; }
    char token[32]; uint32_t len=0; while(s[len]&&s[len]!=' '&&s[len]!='\t'&&len<31){ token[len]=s[len]; len++; } token[len]=0;
    const char *rest=s+len; while(*rest==' '||*rest=='\t') rest++;
    if(*rest){ pc_write("inode: too many args\n"); return 1; }
    uint32_t ino; if(!parse_u32_simple(token,&ino)){ pc_write("inode: invalid number\n"); return 1; }
    struct ext2_stat_info st; int32_t r=pc_ext2_inode(ino,&st);
    if(r<0){ pc_write("inode: error "); pc_write_i64(r); pc_write("\n"); return 1; }
    dump_stat_ext2(&st); return 0;
}
static int command_super(const char *a){
    (void)a;
    struct ext2_super_info si; int32_t r=pc_ext2_super(&si);
    if(r<0){ pc_write("super: error "); pc_write_i64(r); pc_write("\n"); return 1; }
    pc_write("EXT2 super:\n");
    pc_write(" total inodes "); pc_write_u64(si.total_inodes); pc_write("\n");
    pc_write(" total blocks "); pc_write_u64(si.total_blocks); pc_write("\n");
    pc_write(" free inodes "); pc_write_u64(si.free_inodes); pc_write("\n");
    pc_write(" free blocks "); pc_write_u64(si.free_blocks); pc_write("\n");
    pc_write(" blocksize "); pc_write_u64(si.block_size); pc_write("\n");
    pc_write(" groups "); pc_write_u64(si.groups_count); pc_write("\n");
    pc_write(" partition lba "); pc_write_u64(si.partition_lba); pc_write("\n");
    return 0;
}
static int command_blocks(const char *arguments){
    char path[SYSTEM_PATH_CAPACITY];
    if(!resolve_path(arguments,"",path)){ pc_write("blocks: path required\n"); return 1; }
    struct ext2_blocks_info bi; int32_t r=pc_ext2_blocks(path,&bi);
    if(r<0){ pc_write("blocks: error "); pc_write_i64(r); pc_write("\n"); return 1; }
    pc_write("file "); pc_write(path); pc_write(" ino "); pc_write_u64(bi.ino); pc_write("\n");
    pc_write(" blocks "); pc_write_u64(bi.logical_count); pc_write("\n");
    for(uint32_t i=0;i<bi.logical_count;i++){ pc_write("  "); pc_write_u64(i); pc_write(" -> "); pc_write_u64(bi.blocks[i]); pc_write("\n"); }
    return 0;
}
static int command_fsinfo(const char *a){
    (void)a;
    char fstype[32]; char dev[32];
    if(pc_get_fs_type(fstype,sizeof(fstype))>=0){ pc_write("fs type: "); pc_write(fstype); pc_write("\n"); }
    if(pc_get_root_device(dev,sizeof(dev))>=0){ pc_write("device: "); pc_write(dev); pc_write("\n"); }
    struct ext2_super_info si; if(pc_ext2_super(&si)==0){
        pc_write(" free blocks "); pc_write_u64(si.free_blocks); pc_write("/"); pc_write_u64(si.total_blocks); pc_write("\n");
        pc_write(" free inodes "); pc_write_u64(si.free_inodes); pc_write("/"); pc_write_u64(si.total_inodes); pc_write("\n");
    }
    return 0;
}

int system_filesystem_command(const char *name, const char *arguments){
    if(pc_strcmp(name,"ls")==0) return command_ls(arguments);
    if(pc_strcmp(name,"cat")==0) return command_cat(arguments);
    if(pc_strcmp(name,"touch")==0)
        return command_create(name,arguments,false);
    if(pc_strcmp(name,"mkdir")==0)
        return command_create(name,arguments,true);
    if(pc_strcmp(name,"dmesg")==0) return command_dmesg();
    if(pc_strcmp(name,"savelog")==0) return command_savelog();
    if(pc_strcmp(name,"stat")==0) return command_stat(arguments);
    if(pc_strcmp(name,"inode")==0) return command_inode(arguments);
    if(pc_strcmp(name,"super")==0) return command_super(arguments);
    if(pc_strcmp(name,"blocks")==0) return command_blocks(arguments);
    if(pc_strcmp(name,"fsinfo")==0) return command_fsinfo(arguments);
    if(pc_strcmp(name,"dumpi")==0) return command_inode(arguments);
    return -1;
}