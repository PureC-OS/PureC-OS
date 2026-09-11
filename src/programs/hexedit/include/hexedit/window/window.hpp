#pragma once

extern "C" {
#include "../../../../../libgui/include/puregui.h"
}

struct HexWindow
{
    pg_window gui{};
    bool render_active{false};

    bool init();
    void begin();
    void end();
    bool poll(pg_event* ev);
    bool isMinimized() const;
    void shutdown();
    pg_rect client() const;
};

extern HexWindow hex_window;
