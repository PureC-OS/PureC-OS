#include "internal.h"
#include "../libc/include/purec.h"

static uint8_t changed_button(uint8_t changed)
{
    if (changed & 1)
        return 1;
    if (changed & 2)
        return 2;
    if (changed & 4)
        return 3;
    return 0;
}

#define PG_RESIZE_INTERVAL_MS 30
#define PG_DOUBLE_CLICK_MS 400

static bool apply_resize(struct pg_window *window, int32_t mx, int32_t my)
{
    int64_t dx = mx - window->resize_mouse_x;
    int64_t dy = my - window->resize_mouse_y;
    struct pg_rect start = window->resize_start;
    int64_t x = start.x, y = start.y, w = start.width, h = start.height;
    if (window->resize_edges & GUI_EDGE_RIGHT)
        w = start.width + dx;
    if (window->resize_edges & GUI_EDGE_BOTTOM)
        h = start.height + dy;
    if (window->resize_edges & GUI_EDGE_LEFT)
    {
        w = start.width - dx;
        if (w < PG_RESIZE_MIN_WIDTH)
            w = PG_RESIZE_MIN_WIDTH;
        x = (int64_t)start.x + start.width - w;
        if (x < 0)
        {
            w += x;
            x = 0;
        }
    }
    if (window->resize_edges & GUI_EDGE_TOP)
    {
        h = start.height - dy;
        if (h < PG_RESIZE_MIN_HEIGHT)
            h = PG_RESIZE_MIN_HEIGHT;
        y = (int64_t)start.y + start.height - h;
        if (y < 0)
        {
            h += y;
            y = 0;
        }
    }
    if (w < PG_RESIZE_MIN_WIDTH)
        w = PG_RESIZE_MIN_WIDTH;
    if (h < PG_RESIZE_MIN_HEIGHT)
        h = PG_RESIZE_MIN_HEIGHT;
    return pg_window_resize(window, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h);
}

bool pg_window_poll_event(struct pg_window *window, struct pg_event *event)
{
    if (!window || !event || !window->open)
        return false;
    *event = (struct pg_event){.type = PG_EVENT_NONE};
    uint32_t state = pc_gui_window_state();
    bool focused = (state & GUI_WINDOW_STATE_FOCUSED) != 0;
    uint32_t sync = pg_internal_sync_frame(window, state);
    if (window->server && (state & GUI_WINDOW_STATE_CLOSE) && window->closable)
    {
        event->type = PG_EVENT_CLOSE;
        pg_window_close(window);
        return true;
    }
    if (window->server && sync && !(state & GUI_WINDOW_STATE_REPAINT))
    {
        window->focused = focused;
        if (sync & PG_SYNC_MINIMIZED)
            event->type = PG_EVENT_MINIMIZE;
        else if (sync & PG_SYNC_RESIZED)
            event->type = PG_EVENT_REPAINT;
        else
            event->type = PG_EVENT_MOVE;
        return true;
    }
    if (state & GUI_WINDOW_STATE_REPAINT)
    {
        pg_window_fit_display(window);
        window->focused = focused;
        window->repainting = true;
        event->type = PG_EVENT_REPAINT;
        return true;
    }
    if (focused != window->focused)
    {
        window->focused = focused;
        if (focused)
        {
            event->type = PG_EVENT_FOCUS;
            return true;
        }
    }
    if (focused)
    {
        int32_t key = pc_try_getchar();
        if (key >= 0)
        {
            event->key = key;
            /* Gate windows (login) cannot be dismissed with Esc. */
            if (key == 27 && window->closable && window->esc_closes)
            {
                event->type = PG_EVENT_CLOSE;
                pg_window_close(window);
            }
            else
            {
                event->type = PG_EVENT_KEY;
            }
            return true;
        }
        int32_t special = pc_try_get_special();
        if (special >= 0)
        {
            event->key = special;
            event->type = PG_EVENT_SPECIAL_KEY;
            return true;
        }
    }
    struct mouse_state mouse;
    if (!pc_mouse_get(&mouse))
        return false;
    event->x = mouse.x;
    event->y = mouse.y;
    uint8_t changed = mouse.buttons ^ window->previous_mouse_buttons;
    if (changed)
    {
        event->button = changed_button(changed);
        event->type = (mouse.buttons & changed) ? PG_EVENT_MOUSE_DOWN
                                                : PG_EVENT_MOUSE_UP;
    }
    else if (mouse.x != window->previous_mouse_x || mouse.y != window->previous_mouse_y)
    {
        event->type = PG_EVENT_MOUSE_MOVE;
    }
    window->previous_mouse_x = mouse.x;
    window->previous_mouse_y = mouse.y;
    window->previous_mouse_buttons = mouse.buttons;
    if (!focused)
        return false;
    if (window->frameless)
        return event->type != PG_EVENT_NONE;
    if (window->server)
    {
        if (state & GUI_WINDOW_STATE_DRAGGING)
        {
            event->type = PG_EVENT_NONE;
            return false;
        }
        if (event->type == PG_EVENT_MOUSE_DOWN && event->button == 1 && pg_internal_point_in_titlebar(window, mouse.x, mouse.y))
            window->dragging = true;
        if (window->dragging)
        {
            if (!(mouse.buttons & 1))
                window->dragging = false;
            event->type = PG_EVENT_NONE;
            return false;
        }
        return event->type != PG_EVENT_NONE;
    }
    struct pg_rect close = {
        window->frame.x + window->frame.width - 24,
        window->frame.y + 8, 14, 14};
    struct pg_rect minimize = {
        window->frame.x + window->frame.width - 44,
        window->frame.y + 8, 14, 14};
    struct pg_rect maximize = {
        window->frame.x + window->frame.width - 64,
        window->frame.y + 8, 14, 14};
    bool has_maximize = window->resizable && window->closable;
    bool can_resize = has_maximize && !window->maximized && !window->minimized;
    if (window->resizing)
    {
        bool held = (mouse.buttons & 1) != 0;
        bool applied = false;
        if (!held || event->type == PG_EVENT_MOUSE_MOVE)
        {
            uint64_t now = pg_internal_now_ms();
            if (!held || now - window->resize_last_ms >= PG_RESIZE_INTERVAL_MS)
            {
                applied = apply_resize(window, mouse.x, mouse.y);
                window->resize_last_ms = now;
            }
        }
        if (!held)
            window->resizing = false;
        event->type = applied ? PG_EVENT_REPAINT : PG_EVENT_NONE;
        return applied;
    }
    if (event->type == PG_EVENT_MOUSE_DOWN && event->button == 1)
    {
        uint32_t edges = can_resize
                             ? gui_resize_edges((int32_t)window->frame.x, (int32_t)window->frame.y,
                                                (int32_t)window->frame.width,
                                                (int32_t)window->frame.height, mouse.x, mouse.y)
                             : 0;
        if (edges)
        {
            window->resizing = true;
            window->resize_edges = edges;
            window->resize_start = window->frame;
            window->resize_mouse_x = mouse.x;
            window->resize_mouse_y = mouse.y;
            window->resize_last_ms = 0;
            event->type = PG_EVENT_NONE;
            return false;
        }
        struct pg_rect titlebar = {
            window->frame.x, window->frame.y,
            window->frame.width, PG_TITLEBAR_HEIGHT + PG_WINDOW_BORDER * 2};
        bool on_button = pg_internal_point_inside(mouse.x, mouse.y, &close) ||
                         pg_internal_point_inside(mouse.x, mouse.y, &minimize) ||
                         (has_maximize && pg_internal_point_inside(mouse.x, mouse.y, &maximize));
        if (pg_internal_point_inside(mouse.x, mouse.y, &titlebar) && !on_button)
        {
            uint64_t now = pg_internal_now_ms();
            if (has_maximize && window->title_click_ms &&
                now - window->title_click_ms < PG_DOUBLE_CLICK_MS)
            {
                window->title_click_ms = 0;
                if (window->maximized)
                    pg_window_unmaximize(window);
                else
                    pg_window_maximize(window);
                event->type = PG_EVENT_REPAINT;
                return true;
            }
            window->title_click_ms = now;
            window->dragging = true;
            window->drag_offset_x = mouse.x - (int32_t)window->frame.x;
            window->drag_offset_y = mouse.y - (int32_t)window->frame.y;
        }
    }
    if (event->type == PG_EVENT_MOUSE_MOVE && window->dragging && (mouse.buttons & 1))
    {
        bool restored = false;
        if (window->maximized)
        {
            uint32_t old_width = window->frame.width;
            pg_window_unmaximize(window);
            if (old_width && window->frame.width < old_width)
                window->drag_offset_x =
                    (int32_t)((int64_t)window->drag_offset_x * window->frame.width / old_width);
            restored = true;
        }
        int32_t next_x = mouse.x - window->drag_offset_x;
        int32_t next_y = mouse.y - window->drag_offset_y;
        if (next_x < 0)
            next_x = 0;
        if (next_y < 0)
            next_y = 0;
        if (pg_window_move(window, (uint32_t)next_x, (uint32_t)next_y))
            event->type = restored ? PG_EVENT_REPAINT : PG_EVENT_MOVE;
        else if (restored)
            event->type = PG_EVENT_REPAINT;
    }
    if (event->type == PG_EVENT_MOUSE_UP && event->button == 1)
    {
        bool was_dragging = window->dragging;
        window->dragging = false;
        if (!was_dragging && pg_internal_point_inside(mouse.x, mouse.y, &close))
        {
            /* Gate windows (login) cannot be closed or minimized. */
            if (window->closable)
            {
                event->type = PG_EVENT_CLOSE;
                pg_window_close(window);
                return true;
            }
            event->type = PG_EVENT_NONE;
            return false;
        }
        if (window->closable && !was_dragging && pg_internal_point_inside(mouse.x, mouse.y, &minimize))
        {
            window->minimized = !window->minimized;
            (void)pg_internal_update_registered_frame(window);
            pc_desktop_redraw();
            event->type = PG_EVENT_MINIMIZE;
        }
        else if (has_maximize && !was_dragging && !window->minimized &&
                 pg_internal_point_inside(mouse.x, mouse.y, &maximize))
        {
            if (window->maximized)
                pg_window_unmaximize(window);
            else
                pg_window_maximize(window);
            event->type = PG_EVENT_REPAINT;
        }
    }
    if (!(mouse.buttons & 1))
        window->dragging = false;
    return event->type != PG_EVENT_NONE;
}

bool pg_window_poll_key_event(struct pg_window *window, struct key_event *event)
{
    if (!window || !event || !window->open)
        return false;
    bool focused = (pc_gui_window_state() & GUI_WINDOW_STATE_FOCUSED) != 0;
    if (!focused)
        return false;
    return pc_key_event_get(event) == 1;
}

int32_t pg_window_take_wheel(struct pg_window *window)
{
    struct mouse_state mouse;
    if (!window || !window->open || !pc_mouse_get(&mouse))
        return 0;
    if (!window->wheel_ready)
    {
        window->previous_wheel = mouse.wheel;
        window->wheel_ready = true;
        return 0;
    }
    int32_t delta = mouse.wheel - window->previous_wheel;
    window->previous_wheel = mouse.wheel;
    return (pc_gui_window_state() & GUI_WINDOW_STATE_FOCUSED) ? delta : 0;
}
