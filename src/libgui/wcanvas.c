#include "internal.h"
#include "../libc/include/purec.h"
#include "include/pg_canvas.h"

struct pg_canvas *pg_window_canvas(struct pg_window *window)
{
    static struct pg_canvas canvas;
    if (!window || !window->open)
        return 0;
    pg_internal_surface_sync(window);
    uint32_t *surface_px, surface_w, surface_h;
    if (!pg_internal_surface_info(&surface_px, &surface_w, &surface_h))
        return 0;
    uint32_t ox = window->client.x - window->frame.x, oy = window->client.y - window->frame.y;
    if (ox + window->client.width > surface_w || oy + window->client.height > surface_h)
        return 0;
    pgc_init_stride(&canvas, surface_px + (uint64_t)oy * surface_w + ox, (int)window->client.width,
                    (int)window->client.height, (int)surface_w);
    pc_surface_damage((int32_t)ox, (int32_t)oy, window->client.width, window->client.height);
    return &canvas;
}

void pg_window_damage(struct pg_window *window, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!window)
        return;
    pc_surface_damage((int32_t)(window->client.x - window->frame.x + x),
                      (int32_t)(window->client.y - window->frame.y + y), width, height);
}
