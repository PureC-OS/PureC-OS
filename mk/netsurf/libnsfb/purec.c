#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libnsfb.h"
#include "libnsfb_plot.h"
#include "libnsfb_event.h"

#include "nsfb.h"
#include "surface.h"
#include "plot.h"

#include "purec.h"
#include "puregui.h"

#define QUEUE_SIZE 16

struct purec_state {
    struct pg_window window;
    bool open;
    nsfb_event_t queue[QUEUE_SIZE];
    int queue_head;
    int queue_count;
    int mouse_buttons;
    long long last_full_ms;
};

#define FULL_REPAINT_MS 150

static struct purec_state *state_of(nsfb_t *nsfb)
{
    return (struct purec_state *)nsfb->surface_priv;
}

static void queue_push(struct purec_state *state, enum nsfb_event_type_e type,
                       int value)
{
    if (state->queue_count >= QUEUE_SIZE)
        return;
    int slot = (state->queue_head + state->queue_count) % QUEUE_SIZE;
    state->queue[slot].type = type;
    state->queue[slot].value.keycode = (enum nsfb_key_code_e)value;
    state->queue_count++;
}

static void queue_move(struct purec_state *state, int x, int y)
{
    if (state->queue_count >= QUEUE_SIZE)
        return;
    int slot = (state->queue_head + state->queue_count) % QUEUE_SIZE;
    state->queue[slot].type = NSFB_EVENT_MOVE_ABSOLUTE;
    state->queue[slot].value.vector.x = x;
    state->queue[slot].value.vector.y = y;
    state->queue[slot].value.vector.z = 0;
    state->queue_count++;
}

static bool queue_pop(struct purec_state *state, nsfb_event_t *event)
{
    if (state->queue_count == 0)
        return false;
    *event = state->queue[state->queue_head];
    state->queue_head = (state->queue_head + 1) % QUEUE_SIZE;
    state->queue_count--;
    return true;
}

static int purec_defaults(nsfb_t *nsfb)
{
    nsfb->width = 800;
    nsfb->height = 600;
    nsfb->format = NSFB_FMT_XRGB8888;
    select_plotters(nsfb);
    return 0;
}

static int purec_initialise(nsfb_t *nsfb)
{
    struct purec_state *state = calloc(1, sizeof(*state));
    if (state == NULL)
        return -1;
    if (!pg_window_center(&state->window, "NetSurf", (uint32_t)nsfb->width,
                          (uint32_t)nsfb->height)) {
        free(state);
        return -1;
    }
    state->open = true;
    nsfb->surface_priv = state;
    if (state->window.client.width > 0 && state->window.client.height > 0) {
        nsfb->width = (int)state->window.client.width;
        nsfb->height = (int)state->window.client.height;
    }
    nsfb->format = NSFB_FMT_XRGB8888;
    select_plotters(nsfb);
    size_t size = (size_t)nsfb->width * (size_t)nsfb->height * 4;
    nsfb->ptr = calloc(1, size);
    if (nsfb->ptr == NULL) {
        pg_window_close(&state->window);
        free(state);
        nsfb->surface_priv = NULL;
        return -1;
    }
    nsfb->linelen = nsfb->width * 4;
    return 0;
}

static int purec_set_geometry(nsfb_t *nsfb, int width, int height,
                              enum nsfb_format_e format)
{
    (void)format;
    if (width > 0)
        nsfb->width = width;
    if (height > 0)
        nsfb->height = height;
    nsfb->format = NSFB_FMT_XRGB8888;
    select_plotters(nsfb);
    if (nsfb->ptr != NULL) {
        size_t size = (size_t)nsfb->width * (size_t)nsfb->height * 4;
        uint8_t *bigger = realloc(nsfb->ptr, size);
        if (bigger == NULL)
            return -1;
        nsfb->ptr = bigger;
    }
    nsfb->linelen = nsfb->width * 4;
    return 0;
}

static int purec_finalise(nsfb_t *nsfb)
{
    struct purec_state *state = state_of(nsfb);
    if (state != NULL) {
        if (state->open)
            pg_window_close(&state->window);
        free(state);
        nsfb->surface_priv = NULL;
    }
    free(nsfb->ptr);
    nsfb->ptr = NULL;
    return 0;
}

static int purec_blit(nsfb_t *nsfb, int x0, int y0, int x1, int y1)
{
    struct purec_state *state = state_of(nsfb);
    if (state == NULL || !state->open || nsfb->ptr == NULL)
        return 0;
    if (pg_window_is_minimized(&state->window))
        return 0;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > nsfb->width)
        x1 = nsfb->width;
    if (y1 > nsfb->height)
        y1 = nsfb->height;
    if (x1 <= x0 || y1 <= y0)
        return 0;
    const uint32_t *pixels = (const uint32_t *)nsfb->ptr;
    pg_window_begin(&state->window);
    (void)pg_window_blit(&state->window, x0, y0, (uint32_t)(x1 - x0),
                         (uint32_t)(y1 - y0),
                         pixels + (size_t)y0 * (size_t)nsfb->width + (size_t)x0,
                         (uint32_t)nsfb->width);
    pg_window_end(&state->window);
    return 0;
}

static int purec_update(nsfb_t *nsfb, nsfb_bbox_t *box)
{
    return purec_blit(nsfb, box->x0, box->y0, box->x1, box->y1);
}

static int purec_special_to_key(int code)
{
    switch (code) {
    case 4: return NSFB_KEY_HOME;
    case 5: return NSFB_KEY_END;
    case 6: return NSFB_KEY_PAGEUP;
    case 7: return NSFB_KEY_PAGEDOWN;
    case 8: return NSFB_KEY_LEFT;
    case 9: return NSFB_KEY_RIGHT;
    case 10: return NSFB_KEY_UP;
    case 11: return NSFB_KEY_DOWN;
    case 12: return NSFB_KEY_DELETE;
    default: return NSFB_KEY_UNKNOWN;
    }
}

static void purec_push_key(struct purec_state *state, int key)
{
    int shifted = 0;
    if (key == '\n' || key == '\r')
        key = NSFB_KEY_RETURN;
    else if (key >= 'A' && key <= 'Z') {
        key = key - 'A' + 'a';
        shifted = 1;
    }
    if (key == NSFB_KEY_UNKNOWN)
        return;
    if (shifted)
        queue_push(state, NSFB_EVENT_KEY_DOWN, NSFB_KEY_LSHIFT);
    queue_push(state, NSFB_EVENT_KEY_DOWN, key);
    queue_push(state, NSFB_EVENT_KEY_UP, key);
    if (shifted)
        queue_push(state, NSFB_EVENT_KEY_UP, NSFB_KEY_LSHIFT);
}

static int purec_mouse_key(int button)
{
    switch (button) {
    case 2: return NSFB_KEY_MOUSE_3;
    case 3: return NSFB_KEY_MOUSE_2;
    default: return NSFB_KEY_MOUSE_1;
    }
}

static void purec_poll_window(nsfb_t *nsfb, struct purec_state *state)
{
    struct pg_event event;
    while (state->queue_count < QUEUE_SIZE - 4 &&
           pg_window_poll_event(&state->window, &event)) {
        switch (event.type) {
        case PG_EVENT_CLOSE:
            state->queue[(state->queue_head + state->queue_count) % QUEUE_SIZE]
                .type = NSFB_EVENT_CONTROL;
            state->queue[(state->queue_head + state->queue_count) % QUEUE_SIZE]
                .value.controlcode = NSFB_CONTROL_QUIT;
            state->queue_count++;
            break;
        case PG_EVENT_KEY:
            purec_push_key(state, event.key);
            break;
        case PG_EVENT_SPECIAL_KEY:
            purec_push_key(state, purec_special_to_key(event.key));
            break;
        case PG_EVENT_MOUSE_MOVE:
            queue_move(state, event.x - (int)state->window.client.x,
                       event.y - (int)state->window.client.y);
            break;
        case PG_EVENT_MOUSE_DOWN:
            queue_move(state, event.x - (int)state->window.client.x,
                       event.y - (int)state->window.client.y);
            queue_push(state, NSFB_EVENT_KEY_DOWN, purec_mouse_key(event.button));
            break;
        case PG_EVENT_MOUSE_UP:
            queue_move(state, event.x - (int)state->window.client.x,
                       event.y - (int)state->window.client.y);
            queue_push(state, NSFB_EVENT_KEY_UP, purec_mouse_key(event.button));
            break;
        case PG_EVENT_REPAINT:
        case PG_EVENT_MOVE:
        case PG_EVENT_FOCUS:
            purec_blit(nsfb, 0, 0, nsfb->width, nsfb->height);
            break;
        default:
            break;
        }
    }
    int32_t wheel = pg_window_take_wheel(&state->window);
    while (wheel != 0 && state->queue_count < QUEUE_SIZE - 2) {
        int key = wheel > 0 ? NSFB_KEY_MOUSE_4 : NSFB_KEY_MOUSE_5;
        queue_push(state, NSFB_EVENT_KEY_DOWN, key);
        queue_push(state, NSFB_EVENT_KEY_UP, key);
        wheel += wheel > 0 ? -1 : 1;
    }
}

static int64_t purec_now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void purec_maybe_repaint(nsfb_t *nsfb, struct purec_state *state)
{
    long long now = purec_now_ms();
    if (now - state->last_full_ms < FULL_REPAINT_MS)
        return;
    state->last_full_ms = now;
    purec_blit(nsfb, 0, 0, nsfb->width, nsfb->height);
}

static bool purec_input(nsfb_t *nsfb, nsfb_event_t *event, int timeout)
{
    struct purec_state *state = state_of(nsfb);
    if (state == NULL)
        return false;
    int64_t deadline = timeout > 0 ? purec_now_ms() + timeout : 0;
    for (;;) {
        purec_maybe_repaint(nsfb, state);
        if (queue_pop(state, event))
            return true;
        if (!pg_window_is_open(&state->window)) {
            event->type = NSFB_EVENT_CONTROL;
            event->value.controlcode = NSFB_CONTROL_QUIT;
            return true;
        }
        purec_poll_window(nsfb, state);
        if (queue_pop(state, event))
            return true;
        if (timeout == 0)
            return false;
        if (timeout > 0 && purec_now_ms() >= deadline) {
            event->type = NSFB_EVENT_CONTROL;
            event->value.controlcode = NSFB_CONTROL_TIMEOUT;
            return true;
        }
        pc_sleep(8);
    }
}

const nsfb_surface_rtns_t purec_rtns = {
    .defaults = purec_defaults,
    .initialise = purec_initialise,
    .finalise = purec_finalise,
    .geometry = purec_set_geometry,
    .input = purec_input,
    .update = purec_update,
};

NSFB_SURFACE_DEF(purec, NSFB_SURFACE_PUREC, &purec_rtns)
