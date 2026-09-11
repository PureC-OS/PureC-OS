#include "progress.h"
#include "../../libfs/include/purefs.h"
#include "../../libc/include/purec.h"

#define INSTALL_PROGRESS_BAR_WIDTH 44
#define INSTALL_PROGRESS_HEARTBEAT_POLLS 12

static void draw_bar(uint32_t progress){
    char bar[INSTALL_PROGRESS_BAR_WIDTH+3];
    uint32_t filled=progress*INSTALL_PROGRESS_BAR_WIDTH/100;
    bar[0]='[';
    for(uint32_t index=0;index<INSTALL_PROGRESS_BAR_WIDTH;index++){
        if(index<filled) bar[index+1]='=';
        else if(index==filled && progress<100) bar[index+1]='>';
        else bar[index+1]=' ';
    }
    bar[INSTALL_PROGRESS_BAR_WIDTH+1]=']';
    bar[INSTALL_PROGRESS_BAR_WIDTH+2]='\0';
    pc_write(bar);
}

void installer_progress_init(struct installer_progress_view *view){
    if(!view) return;
    view->last_progress=UINT32_MAX;
    view->poll_count=0;
    view->spinner=0;
    view->last_stage[0]='\0';
}

void installer_progress_update(struct installer_progress_view *view,
                               const struct install_status *status,
                               const char *device, bool force){
    if(!view || !status) return;
    bool changed=status->progress!=view->last_progress
        || pc_strcmp(status->stage,view->last_stage)!=0;
    view->poll_count++;
    if(!force && !changed
       && view->poll_count<INSTALL_PROGRESS_HEARTBEAT_POLLS) return;
    view->poll_count=0;
    uint32_t progress=status->progress>100 ? 100 : status->progress;
    static const char activity[]={'|','/','-','\\'};
    pc_console_clear();
    pc_write("Pure OS installation\n");
    pc_write("------------------------------------------------------------\n");
    pc_write("Target: ");
    pc_write(device && device[0] ? device : "unknown");
    pc_write("\n\n");
    draw_bar(progress);
    pc_write("  ");
    pc_write_u64(progress);
    pc_write("%\n\nStage: ");
    pc_write(status->stage[0] ? status->stage : "Preparing");
    pc_write("\nActivity: ");
    char spinner[2]={activity[view->spinner++&3],0};
    pc_write(spinner);
    pc_write("  Do not power off or disconnect the target disk.\n");
    view->last_progress=status->progress;
    pc_copy(view->last_stage,status->stage,sizeof(view->last_stage));
}

const char *installer_progress_error_text(int32_t status){
    switch(status){
        case PF_ERROR_IO: return "disk read/write failed";
        case PF_ERROR_NOT_FOUND: return "required file or target not found (see kernel log)";
        case PF_ERROR_INVALID: return "invalid disk state";
        case PF_ERROR_NO_SPACE: return "not enough space";
        case PF_ERROR_EXISTS: return "file already exists (retry install)";
        case PF_ERROR_NOT_FILE: return "expected file, found directory";
        case PF_ERROR_NOT_DIR: return "target FS corrupted: file blocks directory (retry install)";
        case PF_ERROR_UNSUPPORTED: return "unsupported disk geometry";
        case PF_ERROR_BUSY: return "storage is busy";
        case PF_ERROR_READ_ONLY: return "target is read-only";
        case PF_ERROR_CONFIRMATION: return "disk identity changed";
        case PF_ERROR_NOT_BLANK: return "disk not blank";
        case PF_ERROR_TOO_SMALL: return "target disk is too small";
        default: return "unknown storage error";
    }
}
