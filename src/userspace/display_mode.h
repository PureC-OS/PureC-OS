#pragma once
#include <stdint.h>
#include <stdbool.h>

#define DISPLAY_INI_PATH "/config/display.ini"

/* 0=ok, -1=failed, -2=live switching unsupported on this machine. */
int display_mode_apply(uint32_t width, uint32_t height, uint8_t bpp);
bool display_mode_current(uint32_t *width, uint32_t *height, uint8_t *bpp);
bool display_mode_load(uint32_t *width, uint32_t *height, uint8_t *bpp);
/* Called from userspace_init before the first draw. */
void display_mode_boot_apply(void);
/* Called from the desktop input loop; applies manual ini edits live. */
void display_mode_poll(void);
