#pragma once

#include "ui.h"
#include "setup.h"
#include "../../io/storage/storage_types.h"
#include <stdbool.h>
#include <stdint.h>

#define APP_MAX_DISKS 20
#define APP_STAGE_MAX 48

enum app_page {
    PAGE_WELCOME = 0,
    PAGE_DISK,
    PAGE_COMPUTER,
    PAGE_ACCOUNT,
    PAGE_REVIEW,
    PAGE_INSTALL,
    PAGE_DONE,
    PAGE_FAILED
};

#define APP_PHASES 6

struct ui_input {
    int32_t mx, my;
    bool down;
    bool pressed;
    bool released;
    int32_t key;
    int32_t special;
    int32_t wheel;
};

struct ui {
    struct ui_canvas c;
    struct ui_input in;
    uint32_t frame;
    int32_t active;
    int32_t focus;
    int32_t ids[24];
    int32_t id_count;
    int32_t ids_new[24];
    int32_t id_new_count;
    bool dirty;
    bool caret_on;
};

struct app {
    enum app_page page;
    struct storage_device_info disks[APP_MAX_DISKS];
    int32_t disk_count;
    int32_t selected;
    int32_t scroll;
    struct install_settings s;
    char user_pass2[SETUP_PASSWORD_MAX];
    char root_pass2[SETUP_PASSWORD_MAX];
    bool same_root;
    bool reveal;
    bool username_edited;
    bool erase_ok;
    uint32_t progress_target;
    uint32_t progress_shown;
    char stage[APP_STAGE_MAX];
    int32_t phase;
    int32_t fail_code;
    const char *fail_text;
    bool resumed;
    bool start_install;
    bool do_reboot;
    bool quit;
    bool install_started;
};

void view_init(struct app *a);
void view_background(struct ui_canvas *c, int32_t origin_x, int32_t origin_y,
                     int32_t screen_w, int32_t screen_h);
void view_frame(struct app *a, struct ui *ui);
void ui_focus_next(struct ui *ui, int32_t dir);
