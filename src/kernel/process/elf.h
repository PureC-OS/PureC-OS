#pragma once

#include <stdbool.h>
#include <stdint.h>

struct elf_load_result {
    uint64_t entry;
    uint64_t lowest_address;
    uint64_t highest_address;
};

bool elf_load_user_image(const void *image, uint64_t image_size,
                         uint64_t address_space,
                         struct elf_load_result *result);
