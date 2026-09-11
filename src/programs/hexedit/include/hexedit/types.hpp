#pragma once
#include <stdint.h>
#include <stdbool.h>

constexpr uint32_t HEXEDIT_BYTES_PER_ROW = 16;
constexpr uint32_t HEXEDIT_TOOLBAR_H = 28;
constexpr uint32_t HEXEDIT_HEADER_H = 16;
constexpr uint32_t HEXEDIT_STATUS_H = 26;
constexpr uint32_t HEXEDIT_ROW_H = 16;
constexpr uint32_t HEXEDIT_SIDEBAR_W = 240;
constexpr uint32_t HEXEDIT_OFFSET_CHARS = 10;

enum HexPromptMode {
    HEXEDIT_PROMPT_NONE,
    HEXEDIT_PROMPT_GOTO,
    HEXEDIT_PROMPT_SEARCH
};

struct HexDirEntry {
    char name[64];
    bool is_dir;
    uint64_t size;
};
