#include "settings/app.h"
#include "settings/audio_page.h"
#include "settings/wifi_page.h"
#include "settings/appearance_page.h"
#include "../../../libgui/include/pguiw.h"
#include "../../../libc/include/purec.h"

#define SIDEBAR_WIDTH 176
#define HEADER_HEIGHT 58
#define FOOTER_HEIGHT 34

static bool clicked(const struct pg_window *window,struct pg_rect bounds,
                    const struct pg_event *event){
    if(!window || !event || event->type!=PG_EVENT_MOUSE_UP
       || event->button!=1) return false;
    int32_t x=event->x-(int32_t)window->client.x;
    int32_t y=event->y-(int32_t)window->client.y;
    return x>=(int32_t)bounds.x && y>=(int32_t)bounds.y
        && x<(int32_t)(bounds.x+bounds.width)
        && y<(int32_t)(bounds.y+bounds.height);
}

void settings_app_init(struct settings_app *app){
    settings_model_init(&app->model);
    (void)settings_model_load(&app->model);
    (void)settings_model_apply(&app->model);
    appearance_defaults(&app->appearance);
    (void)appearance_load(&app->appearance);
    app->tab=0;
}

static void draw_sidebar(struct settings_app *app,struct pg_window *window,
                         const struct pg_event *event){
    const char *labels[4]={"Sound","Storage","Network","Appearance"};
    uint32_t content_height=window->client.height-HEADER_HEIGHT-FOOTER_HEIGHT;
    pg_window_rect(window,(struct pg_rect){0,HEADER_HEIGHT,SIDEBAR_WIDTH,
                                          content_height},0x202131);
    pg_window_text(window,18,HEADER_HEIGHT+18,"SYSTEM",0x7F849C);
    for(uint32_t index=0;index<4;index++){
        struct pg_rect item={10,HEADER_HEIGHT+42+index*42,
                             SIDEBAR_WIDTH-20,36};
        pg_window_rect(window,item,app->tab==(int)index
            ? 0x45475A : 0x202131);
        pg_window_text(window,item.x+14,item.y+14,labels[index],
                       app->tab==(int)index
                           ? window->theme.text : window->theme.muted_text);
        if(clicked(window,item,event)) app->tab=(int)index;
    }
}

static void draw_storage_page(struct pg_window *window,
                              const struct pg_event *event){
    (void)event;
    uint32_t left=SIDEBAR_WIDTH+22;
    uint32_t width=window->client.width-left-22;
    pg_window_text(window,left,HEADER_HEIGHT+22,"Storage",window->theme.text);
    pg_window_rect(window,(struct pg_rect){left,HEADER_HEIGHT+48,width,112},
                   0x2B2D40);
    pg_window_text(window,left+18,HEADER_HEIGHT+68,"Disk manager",
                   window->theme.text);
    pg_window_text(window,left+18,HEADER_HEIGHT+90,
                   "Inspect devices, capacity and health.",
                   window->theme.muted_text);
    pg_window_text(window,left+18,HEADER_HEIGHT+108,
                   "Formatting always requires a separate confirmation.",
                   window->theme.muted_text);
    pg_window_text(window,left+18,HEADER_HEIGHT+132,
                   "Open the Disks app from the desktop.",window->theme.accent);
}

void settings_app_draw(struct settings_app *app,struct pg_window *window,
                       const struct pg_event *event){
    /* Live theme preview: the Settings window itself follows the theme. */
    window->theme=pg_theme_by_name(app->appearance.theme);
    pg_window_begin(window);
    if(pg_window_is_minimized(window)){ pg_window_end(window); return; }
    pg_window_clear(window,0x1E1E2E);
    pg_window_rect(window,(struct pg_rect){0,0,window->client.width,
                                          HEADER_HEIGHT},0x292A3D);
    pg_window_text(window,20,18,"System Settings",window->theme.text);
    pg_window_text(window,20,36,
                   "Persistent configuration for PureC OS",0x9399B2);
    draw_sidebar(app,window,event);
    if(app->tab==0) audio_page_draw(window,&app->model,event);
    else if(app->tab==1) draw_storage_page(window,event);
    else if(app->tab==2) wifi_page_draw(window,event);
    else appearance_page_draw(window,&app->appearance,event);
    uint32_t footer_y=window->client.height-FOOTER_HEIGHT;
    pg_window_rect(window,(struct pg_rect){0,footer_y,window->client.width,
                                           FOOTER_HEIGHT},0x292A3D);
    const char *footer=app->tab==0
        ? "Sound changes are applied and saved immediately"
        : app->tab==1 ? "Destructive disk actions require confirmation"
        : app->tab==2 ? "Network driver work is staged for the next iteration"
                      : "Appearance is saved to personalization.conf and applied live";
    pg_window_text(window,18,footer_y+13,footer,0xBAC2DE);
    pg_window_end(window);
}
