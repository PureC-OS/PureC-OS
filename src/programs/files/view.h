#pragma once

#include "app.h"

enum files_action_type {
    FILES_ACTION_NONE=0,
    FILES_ACTION_BACK,
    FILES_ACTION_REFRESH,
    FILES_ACTION_HOME,
    FILES_ACTION_DISKS,
    FILES_ACTION_DIRECTORY,
    FILES_ACTION_ENTRY,
    FILES_ACTION_PREVIOUS_PAGE,
    FILES_ACTION_NEXT_PAGE,
    FILES_ACTION_NEW_FOLDER,
    FILES_ACTION_NEW_FILE,
    FILES_ACTION_RENAME,
    FILES_ACTION_DELETE
};

struct files_action {
    enum files_action_type type;
    int32_t index;
};

struct files_action files_view_draw(struct files_app *app,
                                    const struct pg_event *event);
uint32_t files_view_rows(const struct files_app *app);
