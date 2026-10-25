#include "view.h"
#include "../../libgui/include/pguiw.h"
#include "../../libfs/include/purefs.h"
#include "../../libc/include/purec.h"

#define SIDEBAR_WIDTH 150
#define TOOLBAR_HEIGHT 46
#define ADDRESS_HEIGHT 34
#define HEADER_HEIGHT 28
#define STATUS_HEIGHT 30
#define ROW_HEIGHT 30

static void append(char *buffer, uint32_t capacity, const char *text){
    uint32_t position=pc_strlen(buffer);
    if(position>=capacity) return;
    pc_copy(buffer+position,text,capacity-position);
}

static void append_u64(char *buffer, uint32_t capacity, uint64_t value){
    char digits[21];
    uint32_t position=sizeof(digits);
    digits[--position]='\0';
    do {
        digits[--position]=(char)('0'+value%10);
        value/=10;
    } while(value);
    append(buffer,capacity,digits+position);
}

static void format_size(char *buffer, uint32_t capacity, uint64_t bytes){
    buffer[0]='\0';
    if(bytes>=1024ULL*1024ULL*1024ULL){
        append_u64(buffer,capacity,bytes/(1024ULL*1024ULL*1024ULL));
        append(buffer,capacity," GB");
    } else if(bytes>=1024ULL*1024ULL){
        append_u64(buffer,capacity,bytes/(1024ULL*1024ULL));
        append(buffer,capacity," MB");
    } else if(bytes>=1024ULL){
        append_u64(buffer,capacity,bytes/1024ULL);
        append(buffer,capacity," KB");
    } else {
        append_u64(buffer,capacity,bytes);
        append(buffer,capacity," B");
    }
}

static const char *transport_name(uint8_t transport){
    if(transport==STORAGE_TRANSPORT_USB_MSC
       || transport==STORAGE_TRANSPORT_USB_EHCI) return "USB";
    if(transport==STORAGE_TRANSPORT_AHCI) return "AHCI";
    if(transport==STORAGE_TRANSPORT_ATA_PIO) return "ATA";
    return "Storage";
}

static bool clicked(const struct pg_window *window, struct pg_rect bounds,
                    const struct pg_event *event){
    if(!window || !event || event->type!=PG_EVENT_MOUSE_UP
       || event->button!=1) return false;
    int32_t x=event->x-(int32_t)window->client.x;
    int32_t y=event->y-(int32_t)window->client.y;
    return x>=(int32_t)bounds.x && y>=(int32_t)bounds.y
        && x<(int32_t)(bounds.x+bounds.width)
        && y<(int32_t)(bounds.y+bounds.height);
}

static void draw_folder_icon(struct pg_window *window, uint32_t x,
                             uint32_t y){
    pg_window_rect(window,(struct pg_rect){x+2,y+3,14,6},0xF9E2AF);
    pg_window_rect(window,(struct pg_rect){x+2,y+8,22,15},0xF9E2AF);
    pg_window_rect(window,(struct pg_rect){x+4,y+10,18,3},0xFAB387);
}

static void draw_file_icon(struct pg_window *window, uint32_t x, uint32_t y){
    pg_window_rect(window,(struct pg_rect){x+4,y+2,17,22},0xCDD6F4);
    pg_window_rect(window,(struct pg_rect){x+7,y+8,11,2},0x7F849C);
    pg_window_rect(window,(struct pg_rect){x+7,y+13,11,2},0x7F849C);
    pg_window_rect(window,(struct pg_rect){x+7,y+18,8,2},0x7F849C);
}

uint32_t files_view_rows(const struct files_app *app){
    if(!app || app->window.client.height<260) return 1;
    return (app->window.client.height-TOOLBAR_HEIGHT-ADDRESS_HEIGHT
            -HEADER_HEIGHT-STATUS_HEIGHT)/ROW_HEIGHT;
}

static struct files_action draw_toolbar(struct files_app *app,
                                        const struct pg_event *event){
    struct files_action action={FILES_ACTION_NONE,-1};
    uint32_t width=app->window.client.width;
    pg_window_rect(&app->window,(struct pg_rect){0,0,width,TOOLBAR_HEIGHT},
                   0x252638);
    if(pg_button(&app->window,(struct pg_rect){10,9,38,28},"<",event))
        action.type=FILES_ACTION_BACK;
    if(pg_button(&app->window,(struct pg_rect){54,9,38,28},"R",event))
        action.type=FILES_ACTION_REFRESH;
    if(pg_button(&app->window,(struct pg_rect){104,9,104,28},"New folder",event))
        action.type=FILES_ACTION_NEW_FOLDER;
    if(pg_button(&app->window,(struct pg_rect){214,9,88,28},"New file",event))
        action.type=FILES_ACTION_NEW_FILE;
    if(pg_button(&app->window,(struct pg_rect){width-178,9,78,28},"Rename",event))
        action.type=FILES_ACTION_RENAME;
    if(pg_button(&app->window,(struct pg_rect){width-94,9,84,28},"Delete",event))
        action.type=FILES_ACTION_DELETE;
    return action;
}

static void draw_sidebar(struct files_app *app, const struct pg_event *event,
                         struct files_action *action){
    uint32_t top=TOOLBAR_HEIGHT+ADDRESS_HEIGHT;
    uint32_t height=app->window.client.height-top-STATUS_HEIGHT;
    pg_window_rect(&app->window,(struct pg_rect){0,top,SIDEBAR_WIDTH,height},
                   0x202131);
    pg_window_text(&app->window,16,top+18,"PLACES",0x7F849C);
    struct pg_rect home={8,top+40,SIDEBAR_WIDTH-16,34};
    struct pg_rect disks={8,top+78,SIDEBAR_WIDTH-16,34};
    pg_window_rect(&app->window,home,app->disk_view ? 0x202131 : 0x36384D);
    pg_window_rect(&app->window,disks,app->disk_view ? 0x36384D : 0x202131);
    draw_folder_icon(&app->window,16,top+45);
    pg_window_text(&app->window,48,top+53,"Home",app->window.theme.text);
    pg_window_rect(&app->window,(struct pg_rect){18,top+87,22,17},0x89B4FA);
    pg_window_text(&app->window,48,top+91,"This PC",app->window.theme.text);
    pg_window_text(&app->window,16,top+137,"QUICK ACCESS",0x7F849C);
    pg_window_text(&app->window,24,top+164,"/bin",0xCDD6F4);
    pg_window_text(&app->window,24,top+190,"/bin/program",0xCDD6F4);
    pg_window_text(&app->window,24,top+216,"/game",0xCDD6F4);
    if(clicked(&app->window,home,event)) action->type=FILES_ACTION_HOME;
    if(clicked(&app->window,disks,event)) action->type=FILES_ACTION_DISKS;
    struct pg_rect quick={8,top+150,SIDEBAR_WIDTH-16,78};
    if(clicked(&app->window,quick,event)){
        int32_t local_y=event->y-(int32_t)app->window.client.y-(int32_t)top;
        action->type=FILES_ACTION_DIRECTORY;
        action->index=local_y<181 ? 0 : (local_y<207 ? 1 : 2);
    }
}

static void draw_disks(struct files_app *app, const struct pg_event *event,
                       struct files_action *action){
    uint32_t left=SIDEBAR_WIDTH+18;
    uint32_t top=TOOLBAR_HEIGHT+ADDRESS_HEIGHT+18;
    pg_window_text(&app->window,left,top,"Devices and drives",
                   app->window.theme.text);
    {
        char fs_line[64]={0};
        append(fs_line,sizeof(fs_line),"System file system: ");
        append(fs_line,sizeof(fs_line),app->model.fs_type[0]?app->model.fs_type:"unknown");
        if(app->model.root_device[0]){ append(fs_line,sizeof(fs_line),"  ("); append(fs_line,sizeof(fs_line),app->model.root_device); append(fs_line,sizeof(fs_line),")"); append(fs_line,sizeof(fs_line)," — boot ESP always FAT32"); }
        pg_window_text(&app->window,left,top+16,fs_line,app->window.theme.muted_text);
        top += 14;
    }
    if(app->model.disk_count==0){
        pg_window_text(&app->window,left,top+42,"No storage devices detected",
                       app->window.theme.muted_text);
        return;
    }
    uint32_t card_width=(app->window.client.width-left-18-14)/2;
    for(int32_t index=0;index<app->model.disk_count;index++){
        uint32_t column=(uint32_t)index%2;
        uint32_t row=(uint32_t)index/2;
        struct pg_rect card={left+column*(card_width+14),top+30+row*92,
                             card_width,78};
        pg_window_rect(&app->window,card,0x2B2D40);
        pg_window_rect(&app->window,
                       (struct pg_rect){card.x+12,card.y+17,42,27},0x89B4FA);
        pg_window_rect(&app->window,
                       (struct pg_rect){card.x+18,card.y+45,30,4},0x7F849C);
        const struct storage_device_info *disk=&app->model.disks[index];
        pg_window_text(&app->window,card.x+66,card.y+12,
                       disk->model[0] ? disk->model : disk->name,
                       app->window.theme.text);
        char details[64]={0};
        append(details,sizeof(details),transport_name(disk->transport));
        append(details,sizeof(details),"  ");
        format_size(details+pc_strlen(details),
                    sizeof(details)-pc_strlen(details),
                    disk->sector_count*disk->sector_size);
        pg_window_text(&app->window,card.x+66,card.y+34,details,
                       app->window.theme.muted_text);
        pg_window_text(&app->window,card.x+66,card.y+54,
                       !disk->operational ? "Offline"
                       : (disk->selected ? "System disk - open /"
                                         : "Detected - not mounted"),
                       disk->operational ? 0xA6E3A1 : 0xF38BA8);
        if(clicked(&app->window,card,event) && disk->selected
           && disk->operational){
            action->type=FILES_ACTION_HOME;
            action->index=index;
        }
    }
}

static void draw_entries(struct files_app *app, const struct pg_event *event,
                         struct files_action *action){
    uint32_t left=SIDEBAR_WIDTH;
    uint32_t top=TOOLBAR_HEIGHT+ADDRESS_HEIGHT;
    uint32_t width=app->window.client.width-left;
    pg_window_rect(&app->window,(struct pg_rect){left,top,width,HEADER_HEIGHT},
                   0x292A3D);
    pg_window_text(&app->window,left+42,top+10,"Name",0xBAC2DE);
    pg_window_text(&app->window,left+width-224,top+10,"Type",0xBAC2DE);
    pg_window_text(&app->window,left+width-112,top+10,"Size",0xBAC2DE);
    uint32_t rows=files_view_rows(app);
    uint32_t start=app->page*rows;
    for(uint32_t row=0;row<rows;row++){
        uint32_t index=start+row;
        if(index>=(uint32_t)app->model.entry_count) break;
        uint32_t y=top+HEADER_HEIGHT+row*ROW_HEIGHT;
        struct pg_rect bounds={left,y,width,ROW_HEIGHT};
        uint32_t color=index==(uint32_t)app->selected ? 0x3A496B
                         : (row&1 ? 0x202131 : 0x1E1E2E);
        pg_window_rect(&app->window,bounds,color);
        bool directory=(app->model.entries[index].attributes
                         & PF_ATTR_DIRECTORY)!=0;
        if(directory) draw_folder_icon(&app->window,left+10,y+3);
        else draw_file_icon(&app->window,left+10,y+3);
        pg_window_text(&app->window,left+42,y+11,
                       app->model.entries[index].name,
                       app->window.theme.text);
        pg_window_text(&app->window,left+width-224,y+11,
                       directory ? "File folder" : "File",
                       app->window.theme.muted_text);
        char size[24];
        format_size(size,sizeof(size),app->model.entries[index].size);
        pg_window_text(&app->window,left+width-112,y+11,
                       directory ? "--" : size,app->window.theme.muted_text);
        if(clicked(&app->window,bounds,event)){
            action->type=FILES_ACTION_ENTRY;
            action->index=(int32_t)index;
        }
    }
}

static void draw_status(struct files_app *app, const struct pg_event *event,
                        struct files_action *action){
    uint32_t y=app->window.client.height-STATUS_HEIGHT;
    uint32_t width=app->window.client.width;
    pg_window_rect(&app->window,(struct pg_rect){0,y,width,STATUS_HEIGHT},
                   0x292A3D);
    char summary[48]={0};
    append_u64(summary,sizeof(summary),app->disk_view
               ? (uint64_t)app->model.disk_count
               : (uint64_t)app->model.entry_count);
    append(summary,sizeof(summary),app->disk_view ? " drives" : " items");
    pg_window_text(&app->window,12,y+11,
                   app->status[0] ? app->status : summary,
                   app->status[0] ? 0xF9E2AF : app->window.theme.muted_text);
    if(app->input_mode!=FILES_INPUT_NONE){
        pg_window_rect(&app->window,(struct pg_rect){190,y+4,300,22},0x181825);
        const char *label=app->input_mode==FILES_INPUT_NEW_FOLDER ? "Folder: "
            : app->input_mode==FILES_INPUT_NEW_FILE ? "File: "
            : app->input_mode==FILES_INPUT_RENAME ? "Rename: "
            : "Delete selected? Enter / Esc";
        pg_window_text(&app->window,198,y+11,label,0xCDD6F4);
        if(app->input_mode!=FILES_INPUT_DELETE)
            pg_window_text(&app->window,198+pc_strlen(label)*8,y+11,
                           app->input,0xA6E3A1);
    }
    uint32_t rows=files_view_rows(app);
    uint32_t pages=(app->model.entry_count+(int32_t)rows-1)/(int32_t)rows;
    if(pages>1){
        if(pg_button(&app->window,(struct pg_rect){width-82,y+4,30,22},"<",event))
            action->type=FILES_ACTION_PREVIOUS_PAGE;
        if(pg_button(&app->window,(struct pg_rect){width-44,y+4,30,22},">",event))
            action->type=FILES_ACTION_NEXT_PAGE;
    }
}

struct files_action files_view_draw(struct files_app *app,
                                    const struct pg_event *event){
    struct files_action action={FILES_ACTION_NONE,-1};
    pg_window_begin(&app->window);
    if(pg_window_is_minimized(&app->window)){
        pg_window_end(&app->window);
        return action;
    }
    pg_window_clear(&app->window,0x1E1E2E);
    action=draw_toolbar(app,event);
    pg_window_rect(&app->window,
                   (struct pg_rect){0,TOOLBAR_HEIGHT,
                                    app->window.client.width,ADDRESS_HEIGHT},
                   0x181825);
    pg_window_text(&app->window,14,TOOLBAR_HEIGHT+13,"Location:",0x7F849C);
    pg_window_text(&app->window,92,TOOLBAR_HEIGHT+13,
                   app->disk_view ? "This PC" : app->model.path,0xCDD6F4);
    draw_sidebar(app,event,&action);
    if(app->disk_view) draw_disks(app,event,&action);
    else draw_entries(app,event,&action);
    draw_status(app,event,&action);
    pg_window_end(&app->window);
    return action;
}
