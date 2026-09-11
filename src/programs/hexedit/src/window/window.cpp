#include "hexedit/window/window.hpp"

extern "C" {
#include "../../../../libc/include/purec.h"
}

HexWindow hex_window;

bool HexWindow::init()
{
    pc_display_info d{};

    if (!pc_display_get_info(&d) || !d.available)
    {
        return false;
    }

    uint32_t w = d.width > 64 ? d.width - 64 : d.width;
    uint32_t h = d.height > 64 ? d.height - 64 : d.height;

    if (w > 1100)
    {
        w = 1100;
    }
    if (h > 700)
    {
        h = 700;
    }
    if (w < 860)
    {
        w = 860;
    }
    if (h < 520)
    {
        h = 520;
    }

    render_active = false;
    return pg_window_center(&gui, "HexEdit — PureC", w, h);
}

void HexWindow::begin()
{
    if (!pg_window_is_open(&gui) || render_active)
    {
        return;
    }
    pg_window_begin(&gui);
    render_active = true;
}

void HexWindow::end()
{
    if (!render_active)
    {
        return;
    }
    render_active = false;
    pg_window_end(&gui);
}

bool HexWindow::poll(pg_event* ev)
{
    return pg_window_poll_event(&gui, ev);
}

bool HexWindow::isMinimized() const
{
    return pg_window_is_minimized(&gui);
}

void HexWindow::shutdown()
{
    if (render_active)
    {
        end();
    }
    if (pg_window_is_open(&gui))
    {
        pg_window_close(&gui);
    }
}

pg_rect HexWindow::client() const
{
    return pg_window_client(&gui);
}
