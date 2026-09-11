#include "editor.h"
#include "window.h"
#include "../../libfs/include/purefs.h"
#include "../../libc/include/purec.h"

#define NANO_INITIAL_CAPACITY 4096
#define NANO_READ_CHUNK 256
#define NANO_RENDER_LINE_CAPACITY 128

static char *editor_buffer;
static uint32_t editor_length;
static uint32_t editor_capacity;
static uint32_t editor_cursor;
static uint32_t viewport_line;
static bool editor_dirty;
static const char *editor_path;
static struct nano_window editor_window;

static bool reserve_buffer(uint32_t required){
    if(required<=editor_capacity) return true;
    uint32_t capacity=editor_capacity ? editor_capacity : NANO_INITIAL_CAPACITY;
    while(capacity<required){
        if(capacity>UINT32_MAX/2U){ capacity=required; break; }
        capacity*=2U;
    }
    void *allocation=pc_heap_grow(capacity-editor_capacity);
    if(!allocation) return false;
    if(editor_buffer && allocation!=(void*)(editor_buffer+editor_capacity))
        return false;
    if(!editor_buffer) editor_buffer=(char*)allocation;
    editor_capacity=capacity;
    return true;
}

static uint32_t line_start(uint32_t offset){
    if(offset>editor_length) offset=editor_length;
    while(offset && editor_buffer[offset-1]!='\n') offset--;
    return offset;
}

static uint32_t line_end(uint32_t offset){
    if(offset>editor_length) offset=editor_length;
    while(offset<editor_length && editor_buffer[offset]!='\n') offset++;
    return offset;
}

static uint32_t line_number(uint32_t offset){
    uint32_t line=0;
    if(offset>editor_length) offset=editor_length;
    for(uint32_t index=0;index<offset;index++)
        if(editor_buffer[index]=='\n') line++;
    return line;
}

static uint32_t line_offset(uint32_t target){
    uint32_t line=0;
    for(uint32_t index=0;index<editor_length;index++){
        if(line==target) return index;
        if(editor_buffer[index]=='\n') line++;
    }
    return editor_length;
}

static uint32_t total_lines(void){
    return line_number(editor_length)+1U;
}

static uint32_t content_rows(void){
    uint32_t rows=nano_window_console_rows(&editor_window);
    return rows>4 ? rows-4 : 1;
}

static void ensure_cursor_visible(void){
    uint32_t current=line_number(editor_cursor);
    uint32_t rows=content_rows();
    if(current<viewport_line) viewport_line=current;
    else if(current>=viewport_line+rows) viewport_line=current-rows+1U;
}

static void write_clipped(const char *text,uint32_t length,
                          uint32_t columns,bool newline){
    char line[NANO_RENDER_LINE_CAPACITY];
    uint32_t limit=columns;
    if(limit>=sizeof(line)) limit=sizeof(line)-1;
    if(length>limit) length=limit;
    for(uint32_t index=0;index<length;index++) line[index]=text[index];
    line[length]='\0';
    pc_write(line);
    if(newline) pc_write("\n");
}

static char *append_u32(char *out,uint32_t value){
    char reverse[12]; uint32_t count=0;
    do{ reverse[count++]=(char)('0'+value%10U); value/=10U; }
    while(value && count<sizeof(reverse));
    while(count) *out++=reverse[--count];
    *out='\0'; return out;
}

static char *append_text(char *out,const char *text){
    while(*text) *out++=*text++;
    *out='\0'; return out;
}

static bool redraw(const char *message){
    if(!nano_window_begin_render(&editor_window))
        return nano_window_is_minimized(&editor_window);
    uint32_t columns=nano_window_console_columns(&editor_window);
    write_clipped(editor_dirty ? "PureC nano [modified]" : "PureC nano",
                  editor_dirty ? 21 : 10,columns,true);
    write_clipped("Ctrl+S save | Ctrl+X exit | Home End PgUp PgDn",
                  49,columns,true);
    write_clipped("------------------------------------------------------------",
                  60,columns,true);
    uint32_t offset=line_offset(viewport_line);
    for(uint32_t row=0;row<content_rows();row++){
        uint32_t end=line_end(offset);
        write_clipped(editor_buffer+offset,end-offset,columns,true);
        offset=end<editor_length ? end+1U : editor_length;
    }
    char status[NANO_RENDER_LINE_CAPACITY];
    char *out=append_text(status,message && message[0] ? message : "Ln ");
    if(!message || !message[0]){
        out=append_u32(out,line_number(editor_cursor)+1U);
        out=append_text(out,", Col ");
        (void)append_u32(out,editor_cursor-line_start(editor_cursor)+1U);
    }
    write_clipped(status,pc_strlen(status),columns,false);
    nano_window_end_render(&editor_window);
    return true;
}

static int load_file(void){
    int32_t descriptor=pf_open(editor_path);
    if(descriptor<0) return 0;
    for(;;){
        char chunk[NANO_READ_CHUNK];
        int32_t count=pf_read(descriptor,chunk,sizeof(chunk));
        if(count<0){ (void)pf_close(descriptor); return -1; }
        if(!count) break;
        if((uint32_t)count>UINT32_MAX-editor_length
           || !reserve_buffer(editor_length+(uint32_t)count)){
            (void)pf_close(descriptor); return -2;
        }
        for(int32_t index=0;index<count;index++)
            editor_buffer[editor_length++]=chunk[index];
    }
    (void)pf_close(descriptor);
    return 1;
}

static bool save_file(void){
    if(pf_write_file(editor_path,editor_buffer,editor_length)<0) return false;
    editor_dirty=false;
    return true;
}

static void move_vertical(int32_t lines){
    uint32_t current_line=line_number(editor_cursor);
    uint32_t column=editor_cursor-line_start(editor_cursor);
    int64_t target=(int64_t)current_line+lines;
    if(target<0) target=0;
    uint32_t last=total_lines()-1U;
    if((uint64_t)target>last) target=last;
    uint32_t start=line_offset((uint32_t)target);
    uint32_t end=line_end(start);
    editor_cursor=start+(column>end-start ? end-start : column);
}

static bool handle_special(uint8_t key){
    uint32_t page=content_rows()>1 ? content_rows()-1U : 1U;
    switch(key){
        case KEYBOARD_SPECIAL_HOME: editor_cursor=line_start(editor_cursor); break;
        case KEYBOARD_SPECIAL_END: editor_cursor=line_end(editor_cursor); break;
        case KEYBOARD_SPECIAL_PAGE_UP: move_vertical(-(int32_t)page); break;
        case KEYBOARD_SPECIAL_PAGE_DOWN: move_vertical((int32_t)page); break;
        case KEYBOARD_SPECIAL_LEFT:
            if(editor_cursor) editor_cursor--;
            break;
        case KEYBOARD_SPECIAL_RIGHT:
            if(editor_cursor<editor_length) editor_cursor++;
            break;
        case KEYBOARD_SPECIAL_UP: move_vertical(-1); break;
        case KEYBOARD_SPECIAL_DOWN: move_vertical(1); break;
        case KEYBOARD_SPECIAL_DELETE:
            if(editor_cursor<editor_length){
                for(uint32_t index=editor_cursor;index+1<editor_length;index++)
                    editor_buffer[index]=editor_buffer[index+1];
                editor_length--; editor_dirty=true;
            }
            break;
        default: return false;
    }
    ensure_cursor_visible();
    return true;
}

int nano_run(const char *path){
    editor_path=path; editor_buffer=0; editor_length=0; editor_capacity=0;
    editor_cursor=0; viewport_line=0; editor_dirty=false;
    if(!reserve_buffer(NANO_INITIAL_CAPACITY)) return 1;
    int loaded=load_file();
    if(loaded<0) return 1;
    if(!nano_window_init(&editor_window)) return 1;
    if(!redraw(loaded ? "" : "New file.")) return 1;
    bool exit_armed=false;
    for(;;){
        struct pg_event event;
        if(!nano_window_poll_event(&editor_window,&event)){
            pc_sleep(8); continue;
        }
        if(event.type==PG_EVENT_CLOSE){ nano_window_shutdown(&editor_window); return 0; }
        if(event.type==PG_EVENT_MOVE || event.type==PG_EVENT_MINIMIZE
           || event.type==PG_EVENT_FOCUS || event.type==PG_EVENT_REPAINT){
            (void)redraw(""); continue;
        }
        if(event.type==PG_EVENT_SPECIAL_KEY){
            if(handle_special((uint8_t)event.key)){ exit_armed=false; (void)redraw(""); }
            continue;
        }
        if(event.type!=PG_EVENT_KEY || nano_window_is_minimized(&editor_window))
            continue;
        char character=(char)event.key;
        if(character==19){ exit_armed=false; (void)redraw(save_file()?"Saved.":"Save failed."); continue; }
        if(character==24){
            if(editor_dirty && !exit_armed){ exit_armed=true; (void)redraw("Unsaved: Ctrl+X again discards changes."); continue; }
            nano_window_shutdown(&editor_window); return 0;
        }
        if(character=='\b' || character==127){
            if(editor_cursor){
                for(uint32_t index=editor_cursor-1;index+1<editor_length;index++)
                    editor_buffer[index]=editor_buffer[index+1];
                editor_cursor--; editor_length--; editor_dirty=true;
                ensure_cursor_visible(); (void)redraw("");
            }
            continue;
        }
        if(character=='\r') character='\n';
        if(character!='\n' && character!='\t'
           && (character<' ' || character>'~')) continue;
        if(editor_length==UINT32_MAX || !reserve_buffer(editor_length+1U)){
            (void)redraw("Not enough memory."); continue;
        }
        for(uint32_t index=editor_length;index>editor_cursor;index--)
            editor_buffer[index]=editor_buffer[index-1];
        editor_buffer[editor_cursor++]=character;
        editor_length++; editor_dirty=true; exit_armed=false;
        ensure_cursor_visible(); (void)redraw("");
    }
}
