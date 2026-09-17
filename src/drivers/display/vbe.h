#pragma once
#include <stdint.h>
#include <stdbool.h>

#define VBE_MAX_FRAMEBUFFER_BYTES (16ULL*1024ULL*1024ULL)

bool vbe_is_available(void);
uint64_t vbe_video_memory_bytes(void);
bool vbe_framebuffer_phys(uint64_t *physical_out);
bool vbe_set_mode(uint32_t width, uint32_t height, uint8_t bpp);
volatile void *vbe_map_framebuffer(uint64_t physical, uint64_t size);
