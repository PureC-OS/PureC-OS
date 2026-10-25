#include "model.h"
#include "../../libfs/include/purefs.h"
#include "../../libc/include/purec.h"

static bool is_directory(const struct pf_entry *entry){
    return pf_is_dir(entry);
}

static void sort_entries(struct files_model *model){
    for(int32_t left=0;left<model->entry_count;left++){
        for(int32_t right=left+1;right<model->entry_count;right++){
            bool left_directory=is_directory(&model->entries[left]);
            bool right_directory=is_directory(&model->entries[right]);
            if((right_directory && !left_directory)
               || (right_directory==left_directory
                   && pc_strcmp(model->entries[right].name,
                                model->entries[left].name)<0)){
                struct pf_entry temporary=model->entries[left];
                model->entries[left]=model->entries[right];
                model->entries[right]=temporary;
            }
        }
    }
}

void files_model_init(struct files_model *model){
    if(!model) return;
    *model=(struct files_model){0};
    pc_copy(model->path,"/",sizeof(model->path));
    (void)files_model_refresh(model);
}

bool files_model_refresh(struct files_model *model){
    if(!model) return false;
    model->disk_count=pc_list_disks(model->disks,FILES_DISK_CAPACITY);
    if(model->disk_count<0) model->disk_count=0;
    if(pc_get_fs_type(model->fs_type, sizeof(model->fs_type)) < 0) pc_copy(model->fs_type, "fat32", sizeof(model->fs_type));
    if(pc_get_root_device(model->root_device, sizeof(model->root_device)) < 0) model->root_device[0]='\0';
    {
        char cfg[128]={0};
        int32_t n = pf_read_all("/purec/install.cfg", cfg, sizeof(cfg)-1);
        if(n>0){
            cfg[n]='\0';
            for(int32_t i=0; cfg[i] && i+12 < n; i++){
                if(cfg[i]=='e' && cfg[i+1]=='x' && cfg[i+2]=='t' && cfg[i+3]=='2'){ pc_copy(model->fs_type, "ext2", sizeof(model->fs_type)); break; }
                if(cfg[i]=='f' && cfg[i+1]=='a' && cfg[i+2]=='t' && cfg[i+3]=='3' && cfg[i+4]=='2'){ pc_copy(model->fs_type, "fat32", sizeof(model->fs_type)); break; }
            }
        }
    }
    model->entry_count=pf_list(model->path,model->entries,
                                         FILES_ENTRY_CAPACITY);
    model->error=model->entry_count<0 ? model->entry_count : 0;
    if(model->entry_count<0) model->entry_count=0;
    sort_entries(model);
    return model->error==0;
}

bool files_model_enter(struct files_model *model, uint32_t index){
    if(!model || index>=(uint32_t)model->entry_count
       || !is_directory(&model->entries[index])) return false;
    char next[FILES_PATH_CAPACITY];
    if(!files_path_join(next,sizeof(next),model->path,
                        model->entries[index].name)) return false;
    pc_copy(model->path,next,sizeof(model->path));
    if(files_model_refresh(model)) return true;
    (void)files_path_parent(model->path,sizeof(model->path));
    (void)files_model_refresh(model);
    return false;
}

bool files_model_up(struct files_model *model){
    if(!model || !files_path_parent(model->path,sizeof(model->path)))
        return false;
    return files_model_refresh(model);
}

bool files_model_create(struct files_model *model, const char *name,
                        bool directory){
    char path[FILES_PATH_CAPACITY];
    if(!model || !files_path_join(path,sizeof(path),model->path,name))
        return false;
    int32_t result=directory ? pf_create_dir(path) : pf_create_file(path);
    model->error=result;
    if(result<0) return false;
    return files_model_refresh(model);
}

bool files_model_delete(struct files_model *model, uint32_t index){
    char path[FILES_PATH_CAPACITY];
    if(!model || index>=(uint32_t)model->entry_count
       || !files_path_join(path,sizeof(path),model->path,
                           model->entries[index].name)) return false;
    int32_t result=pf_delete(path);
    model->error=result;
    if(result<0) return false;
    return files_model_refresh(model);
}

bool files_model_rename(struct files_model *model, uint32_t index,
                        const char *new_name){
    char path[FILES_PATH_CAPACITY];
    if(!model || index>=(uint32_t)model->entry_count || !new_name[0]
       || !files_path_join(path,sizeof(path),model->path,
                           model->entries[index].name)) return false;
    int32_t result=pf_rename(path,new_name);
    model->error=result;
    if(result<0) return false;
    return files_model_refresh(model);
}
