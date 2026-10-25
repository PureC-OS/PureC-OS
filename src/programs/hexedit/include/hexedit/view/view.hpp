#pragma once
#include "../types.hpp"

extern HexPromptMode hex_prompt;
extern char hex_prompt_buf[64];
extern char hex_status_msg[96];
extern bool hex_status_err;

void hex_enter_prompt(HexPromptMode m);
void hex_exit_prompt(bool ok);
bool hex_do_draw();
