#pragma once

struct terminal_window;

int shell_run(struct terminal_window *terminal, const char *initial_command);
