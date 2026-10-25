#include "app.h"
#include "view.h"
#include "../../libfs/include/purefs.h"
#include "../../libc/include/purec.h"

static void set_status(struct files_app *app, const char *text){
    pc_copy(app->status,text,sizeof(app->status));
}

static void reset_selection(struct files_app *app){
    app->selected=-1;
    app->page=0;
    app->input_mode=FILES_INPUT_NONE;
    app->input[0]='\0';
}

static void refresh(struct files_app *app){
    if(files_model_refresh(&app->model)) set_status(app,"");
    else set_status(app,"Cannot read this location");
    reset_selection(app);
}

static void open_path(struct files_app *app, const char *path){
    pc_copy(app->model.path,path,sizeof(app->model.path));
    app->disk_view=false;
    refresh(app);
}

static void begin_input(struct files_app *app, enum files_input_mode mode){
    if((mode==FILES_INPUT_RENAME || mode==FILES_INPUT_DELETE)
       && app->selected<0){
        set_status(app,"Select an item first");
        return;
    }
    app->input_mode=mode;
    app->input[0]='\0';
    if(mode==FILES_INPUT_RENAME)
        pc_copy(app->input,app->model.entries[app->selected].name,
                sizeof(app->input));
    set_status(app,"");
}

static bool selected_is_directory(const struct files_app *app){
    return app->selected>=0
        && (app->model.entries[app->selected].attributes
             & PF_ATTR_DIRECTORY)!=0;
}

static bool is_image_filename(const char *name) {
    if (!name) return false;
    uint32_t len = pc_strlen(name);
    if (len < 4) return false;
    const char *ext = name + len - 4;
    if (pc_strcmp(ext, ".bmp") == 0 || pc_strcmp(ext, ".BMP") == 0 ||
        pc_strcmp(ext, ".png") == 0 || pc_strcmp(ext, ".PNG") == 0 ||
        pc_strcmp(ext, ".ppm") == 0 || pc_strcmp(ext, ".PPM") == 0 ||
        pc_strcmp(ext, ".jpg") == 0 || pc_strcmp(ext, ".JPG") == 0 ||
        pc_strcmp(ext, ".raw") == 0 || pc_strcmp(ext, ".RAW") == 0 ||
        pc_strcmp(ext, ".img") == 0 || pc_strcmp(ext, ".IMG") == 0) return true;
    if (len >= 5) {
        const char *ext5 = name + len - 5;
        if (pc_strcmp(ext5, ".jpeg") == 0 || pc_strcmp(ext5, ".JPEG") == 0) return true;
    }
    return false;
}

static void open_selected(struct files_app *app){
    if(app->selected<0) return;
    if(selected_is_directory(app)){
        if(files_model_enter(&app->model,(uint32_t)app->selected))
            reset_selection(app);
        return;
    }
    char path[FILES_PATH_CAPACITY];
    if(!files_path_join(path,sizeof(path),app->model.path,
                        app->model.entries[app->selected].name)){
        set_status(app,"Path is too long");
        return;
    }
    if(pc_strlen(path)>=128){
        set_status(app,"Path is too long");
        return;
    }
    const char *viewer = is_image_filename(app->model.entries[app->selected].name)
        ? "/bin/program/imgview" : "/bin/program/nano";
    int32_t pid=pc_exec_with_args(viewer,path);
    if(pid<0){
        set_status(app,"Cannot open viewer");
        return;
    }
    int32_t status=0;
    (void)pc_wait(pid,&status,false);
    pc_desktop_redraw();
    refresh(app);
}

static void submit_input(struct files_app *app){
    bool success=false;
    if(app->input_mode==FILES_INPUT_NEW_FOLDER)
        success=files_model_create(&app->model,app->input,true);
    else if(app->input_mode==FILES_INPUT_NEW_FILE)
        success=files_model_create(&app->model,app->input,false);
    else if(app->input_mode==FILES_INPUT_RENAME)
        success=files_model_rename(&app->model,(uint32_t)app->selected,
                                   app->input);
    else if(app->input_mode==FILES_INPUT_DELETE)
        success=files_model_delete(&app->model,(uint32_t)app->selected);
    if(success){
        reset_selection(app);
        set_status(app,"Done");
    } else {
        app->input_mode=FILES_INPUT_NONE;
        set_status(app,"Operation failed");
    }
}

static void handle_key(struct files_app *app, int32_t key){
    if(app->input_mode!=FILES_INPUT_NONE){
        if(key==27){
            app->input_mode=FILES_INPUT_NONE;
            app->input[0]='\0';
            return;
        }
        if(key=='\r' || key=='\n'){
            if(app->input_mode==FILES_INPUT_DELETE || app->input[0])
                submit_input(app);
            return;
        }
        uint32_t length=pc_strlen(app->input);
        if((key=='\b' || key==127) && length){
            app->input[length-1]='\0';
        } else if(key>=' ' && key<='~'
                  && length+1<sizeof(app->input)){
            app->input[length]=(char)key;
            app->input[length+1]='\0';
        }
        return;
    }
    if(key=='\b' || key==127){
        if(!app->disk_view && files_model_up(&app->model)) reset_selection(app);
    } else if((key=='\r' || key=='\n') && app->selected>=0){
        open_selected(app);
    } else if(key=='h' || key=='H'){
        if(app->selected>=0 && !selected_is_directory(app)){
            char path[FILES_PATH_CAPACITY];
            if(!files_path_join(path,sizeof(path),app->model.path,app->model.entries[app->selected].name)){
                set_status(app,"Path is too long"); return;
            }
            if(pc_strlen(path)>=128){ set_status(app,"Path is too long for hexedit"); return; }
            int32_t pid=pc_exec_with_args("/bin/program/hexedit",path);
            if(pid<0){ set_status(app,"Cannot open hexedit"); return; }
            int32_t status=0; (void)pc_wait(pid,&status,false);
            pc_desktop_redraw(); refresh(app);
        }
    } else if(key=='r' || key=='R'){
        refresh(app);
    }
}

static void handle_action(struct files_app *app, struct files_action action){
    static const char *quick_paths[]={"/bin","/bin/program","/game"};
    switch(action.type){
        case FILES_ACTION_BACK:
            if(app->disk_view) open_path(app,"/");
            else if(files_model_up(&app->model)) reset_selection(app);
            break;
        case FILES_ACTION_REFRESH: refresh(app); break;
        case FILES_ACTION_HOME: open_path(app,"/"); break;
        case FILES_ACTION_DISKS:
            (void)files_model_refresh(&app->model);
            app->disk_view=true;
            reset_selection(app);
            break;
        case FILES_ACTION_DIRECTORY:
            if(action.index>=0 && action.index<3)
                open_path(app,quick_paths[action.index]);
            break;
        case FILES_ACTION_ENTRY:
            if(action.index==app->selected) open_selected(app);
            else app->selected=action.index;
            break;
        case FILES_ACTION_PREVIOUS_PAGE:
            if(app->page) app->page--;
            break;
        case FILES_ACTION_NEXT_PAGE: {
            uint32_t rows=files_view_rows(app);
            if((app->page+1)*rows<(uint32_t)app->model.entry_count)
                app->page++;
            break;
        }
        case FILES_ACTION_NEW_FOLDER:
            begin_input(app,FILES_INPUT_NEW_FOLDER);
            break;
        case FILES_ACTION_NEW_FILE:
            begin_input(app,FILES_INPUT_NEW_FILE);
            break;
        case FILES_ACTION_RENAME: begin_input(app,FILES_INPUT_RENAME); break;
        case FILES_ACTION_DELETE: begin_input(app,FILES_INPUT_DELETE); break;
        default: break;
    }
}

int files_app_run(void){
    struct pc_display_info display;
    if(!pc_display_get_info(&display) || !display.available) return 1;
    uint32_t width=display.width>980 ? 940 : display.width-24;
    uint32_t height=display.height>680 ? 640 : display.height-44;
    if(width<640 || height<400) return 1;
    struct files_app app={.selected=-1};
    if(!pg_window_center(&app.window,"Files - PureC Explorer",width,height))
        return 1;
    files_model_init(&app.model);
    struct pg_event event={.type=PG_EVENT_NONE};
    (void)files_view_draw(&app,&event);
    while(pg_window_is_open(&app.window)){
        if(!pg_window_poll_event(&app.window,&event)){
            pc_sleep(16);
            continue;
        }
        if(event.type==PG_EVENT_CLOSE) break;
        if(event.type==PG_EVENT_MOUSE_MOVE) continue;
        if(event.type==PG_EVENT_KEY) handle_key(&app,event.key);
        struct files_action action=files_view_draw(&app,&event);
        handle_action(&app,action);
        if(action.type!=FILES_ACTION_NONE){
            event.type=PG_EVENT_NONE;
            (void)files_view_draw(&app,&event);
        }
    }
    return 0;
}
