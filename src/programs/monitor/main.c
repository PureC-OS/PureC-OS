#include "../../libgui/include/puregui.h"
#include "../../libc/include/purec.h"

#define MONITOR_WIDTH 720
#define MONITOR_HEIGHT 600
#define MONITOR_MAX_PROCESSES 64
#define MONITOR_PROCESS_QUERY 16
#define MONITOR_MAX_CORES 16
#define MIB (1024ULL*1024ULL)

static struct cpu_core_info prev_cores={0};
static bool prev_cores_valid=false;

static uint32_t core_usage_live(uint32_t index,
                                const struct cpu_core_info *cur){
    if(!prev_cores_valid || index>=prev_cores.count || index>=cur->count)
        return 0;
    uint64_t total_a=prev_cores.cores[index].total_ticks;
    uint64_t idle_a=prev_cores.cores[index].idle_ticks;
    uint64_t total_b=cur->cores[index].total_ticks;
    uint64_t idle_b=cur->cores[index].idle_ticks;
    uint64_t elapsed=total_b>total_a ? total_b-total_a : 0;
    uint64_t idle_elapsed=idle_b>idle_a ? idle_b-idle_a : 0;
    if(!elapsed) return 0;
    if(idle_elapsed>elapsed) idle_elapsed=elapsed;
    return 100U-(uint32_t)((idle_elapsed*100)/elapsed);
}

static char *append_text(char *output, const char *text){
    while(*text) *output++=*text++;
    *output='\0';
    return output;
}

static char *append_u64(char *output, uint64_t value){
    char reverse[24];
    uint32_t length=0;
    if(!value) reverse[length++]='0';
    while(value && length<sizeof(reverse)){
        reverse[length++]=(char)('0'+value%10);
        value/=10;
    }
    while(length) *output++=reverse[--length];
    *output='\0';
    return output;
}

static const char *state_name(const struct process_monitor_info *process){
    if(process->state==PROCESS_MONITOR_STATE_EXITED) return "dead";
    if(process->state==PROCESS_MONITOR_STATE_RUNNING) return "alive";
    return "ready";
}

static void draw_header(struct pg_window *window,
                        const struct cpu_monitor_info *cpu,
                        const struct memory_monitor_info *memory){
    char line[120];
    char *position=append_text(line,"CPU: ");
    position=append_u64(position,cpu->usage_percent);
    position=append_text(position,"%   RAM: ");
    position=append_u64(position,memory->used_bytes/MIB);
    position=append_text(position," / ");
    position=append_u64(position,memory->total_bytes/MIB);
    (void)append_text(position," MiB");
    pg_window_text(window,18,18,line,window->theme.text);
}

static uint32_t draw_cores(struct pg_window *window,
                           const struct cpu_core_info *cores){
    uint32_t count=cores->count;
    if(count>MONITOR_MAX_CORES) count=MONITOR_MAX_CORES;
    pg_window_text(window,18,42,"Per-core (live, 500ms):",
                   window->theme.muted_text);
    uint32_t y=60;
    for(uint32_t i=0;i<count;i++){
        uint32_t usage=core_usage_live(i,cores);
        char label[32];
        char *p=append_text(label,"cpu");
        p=append_u64(p,cores->cores[i].id);
        if(!cores->cores[i].online){
            p=append_text(p," offline");
            pg_window_text(window,18,y,label,window->theme.muted_text);
        } else {
            p=append_text(p," ");
            p=append_u64(p,usage);
            (void)append_text(p,"%");
            pg_window_text(window,18,y,label,window->theme.text);
            uint32_t bar_x=110;
            uint32_t bar_w=window->client.width>bar_x+36
                ? window->client.width-bar_x-18 : 60;
            pg_window_rect(window,
                (struct pg_rect){bar_x,y,bar_w,10},window->theme.border);
            uint32_t fill=(bar_w*usage)/100;
            if(fill) pg_window_rect(window,
                (struct pg_rect){bar_x,y,fill,10},window->theme.accent);
        }
        y+=18;
    }
    return y;
}

static void draw_process_header(struct pg_window *window, uint32_t y){
    pg_window_text(window,18,y,
        "PID   PPID  STATE   CPU    RAM MiB   TIME ms    PROCESS",
        window->theme.muted_text);
    pg_window_rect(window,(struct pg_rect){18,y+18,window->client.width-36,1},
                   window->theme.border);
}

static void draw_process(struct pg_window *window, uint32_t row,
                         uint32_t base_y,
                         const struct process_monitor_info *process){
    char line[128];
    char *position=line;
    position=append_u64(position,process->pid);
    position=append_text(position,"     ");
    position=append_u64(position,process->parent_pid);
    position=append_text(position,"     ");
    position=append_text(position,state_name(process));
    position=append_text(position,"     ");
    position=append_u64(position,process->cpu_percent);
    position=append_text(position,"%     ");
    position=append_u64(position,process->resident_bytes/MIB);
    position=append_text(position,"        ");
    position=append_u64(position,process->runtime_ms);
    position=append_text(position,"       ");
    (void)append_text(position,process->name);
    uint32_t color=process->state==PROCESS_MONITOR_STATE_EXITED
        ? window->theme.danger : window->theme.text;
    pg_window_text(window,18,base_y+row*22,line,color);
}

static void redraw(struct pg_window *window){
    struct cpu_monitor_info cpu={0};
    struct memory_monitor_info memory={0};
    struct cpu_core_info cores={0};
    struct process_monitor_info processes[MONITOR_MAX_PROCESSES];
    int32_t count=pc_process_list(processes,MONITOR_PROCESS_QUERY);
    bool available=pc_cpu_info(&cpu) && pc_memory_info(&memory) && count>=0;
    bool cores_ok=pc_cpu_core_info(&cores)>=0;
    pg_window_begin(window);
    if(available){
        draw_header(window,&cpu,&memory);
        uint32_t list_y=70;
        if(cores_ok) list_y=draw_cores(window,&cores)+6;
        draw_process_header(window,list_y);
        list_y+=28;
        uint32_t visible=(uint32_t)count;
        if(visible>MONITOR_MAX_PROCESSES) visible=MONITOR_MAX_PROCESSES;
        /* Fit processes into visible client area. */
        uint32_t max_rows=0;
        if(window->client.height>list_y+10)
            max_rows=(window->client.height-list_y-10)/22;
        if(visible>max_rows) visible=max_rows;
        for(uint32_t index=0;index<visible;index++)
            draw_process(window,index,list_y,&processes[index]);
        if(cores_ok){
            prev_cores=cores;
            prev_cores_valid=true;
        }
    } else {
        pg_window_text(window,18,18,"Monitoring data unavailable",
                       window->theme.danger);
    }
    pg_window_end(window);
}

static int monitor_main(void){
    struct pg_window window;
    struct pc_display_info display;
    if(!pc_display_get_info(&display) || !display.available) return 1;
    uint32_t width=display.width>MONITOR_WIDTH+20
        ? MONITOR_WIDTH : display.width-20;
    uint32_t height=display.height>MONITOR_HEIGHT+40
        ? MONITOR_HEIGHT : display.height-40;
    if(!pg_window_center(&window,"System Monitor",width,height)) return 1;
    redraw(&window);
    uint32_t refresh_elapsed=0;
    while(pg_window_is_open(&window)){
        struct pg_event event;
        if(pg_window_poll_event(&window,&event)){
            if(event.type==PG_EVENT_CLOSE) break;
            if(event.type==PG_EVENT_REPAINT || event.type==PG_EVENT_MOVE
               || event.type==PG_EVENT_FOCUS) redraw(&window);
        }
        pc_sleep(16);
        refresh_elapsed+=16;
        if(refresh_elapsed>=500){
            refresh_elapsed=0;
            if(!pg_window_is_minimized(&window)) redraw(&window);
        }
    }
    pg_window_close(&window);
    return 0;
}

void _start(void){ pc_exit(monitor_main()); }
