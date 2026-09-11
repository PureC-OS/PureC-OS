#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "model.h"
#include "../../libfs/include/purefs.h"
#include "../../libgui/include/puregui.h"

enum files_input_mode {
    FILES_INPUT_NONE=0,
    FILES_INPUT_NEW_FOLDER,
    FILES_INPUT_NEW_FILE,
    FILES_INPUT_RENAME,
    FILES_INPUT_DELETE
};

struct files_app {
    struct pg_window window;
    struct files_model model;
    enum files_input_mode input_mode;
    char input[PF_NAME_CAPACITY];
    char status[80];
    int32_t selected;
    uint32_t page;
    bool disk_view;
};

int files_app_run(void);
