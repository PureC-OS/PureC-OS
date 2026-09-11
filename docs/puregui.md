# PureGUI API V1.0.2

PureGUI is the small ring-3 GUI toolkit shipped with PureC OS. It translates
client-relative coordinates into framebuffer operations and keeps window,
theme and input policy out of applications.

## Artifacts

| Path | Purpose |
|---|---|
| `/include/puregui.h` | Core window, drawing and event API |
| `/include/pguiw.h` | Labels, panels and buttons |
| `/lib/libpuregui.a` | Core implementation |
| `/lib/libpguiw.a` | Optional widgets layer |
| `/bin/gui-demo` | Runnable example |
| `/bin/program/files` | PureGUI file manager client |

The widgets archive depends on the core archive, and the core archive depends
on `libpurec.a`. Link them in this order:

```sh
x86_64-elf-ld -T linker-userspace.ld -o my-app my-app.o \
  libpguiw.a libpuregui.a libpurec.a
```

The system terminal is a real PureGUI client. Its shell input is handled as
normalized window events, while the ring-3 console syscalls constrain shell
and child-process text output to the window client rectangle. The console
backend retains a character grid so the terminal can repaint after a
full-screen child, minimize/restore, or a window move. The installer is a
standalone privileged process that uses the same terminal-window adapter and
clears only its own bounded console region.

The system file manager is also a ring-3 PureGUI client. Its path handling,
directory model, storage-device model and Explorer-style view are separate
modules under `src/programs/files`; none of its window or filesystem policy is
linked into the kernel.

## Minimal application

```c
#include <puregui.h>
#include <pguiw.h>
#include <purec.h>

void _start(void){
    struct pg_window window;
    if(!pg_window_center(&window,"Hello",420,220)) pc_exit(1);

    struct pg_event event={.type=PG_EVENT_NONE};
    while(pg_window_is_open(&window)){
        pg_window_begin(&window);
        pg_label(&window,24,24,"Hello from PureGUI");
        pg_window_end(&window);

        if(pg_window_poll_event(&window,&event)
           && event.type==PG_EVENT_CLOSE) break;
        pc_sleep(16);
    }
    pc_exit(0);
}
```

## Core contract

| Function | Contract |
|---|---|
| `pg_version` | Returns the packaged semantic version string `V1.0.1`. |
| `pg_theme_default` | Returns the built-in immutable color scheme by value. |
| `pg_window_init` | Opens a window at screen coordinates; returns `false` for an invalid size, missing display or a window larger than the display. |
| `pg_window_center` | Opens a centered window with the same validation rules. |
| `pg_window_begin` | Starts an atomic framebuffer update and draws window chrome. |
| `pg_window_end` | Finishes the update started by `pg_window_begin`. |
| `pg_window_close` | Marks a window closed without terminating the process. |
| `pg_window_move` | Asks the desktop to restore its scene, then moves and clamps an open window to the display; the application redraws after the matching move event. |
| `pg_window_minimize` | Collapses an open window to its title bar. |
| `pg_window_restore` | Restores a minimized window and its client area. |
| `pg_window_is_open` | Reports whether the window remains active. |
| `pg_window_is_minimized` | Reports whether an open window is collapsed. |
| `pg_window_client` | Returns the absolute client rectangle, or an empty rectangle for `NULL`. |
| `pg_window_clear` | Fills the client area with a caller-selected color. |
| `pg_window_rect` | Draws a clipped rectangle using client-relative coordinates. |
| `pg_window_text` | Draws clipped fixed-width text using client-relative coordinates. |
| `pg_window_poll_event` | Returns one normalized input, focus or repaint event; title-bar dragging produces `PG_EVENT_MOVE`, focus changes produce `PG_EVENT_FOCUS`, and ordered restoration produces `PG_EVENT_REPAINT`. |

All functions are allocation-free. Invalid window pointers are ignored by
drawing operations. Applications must pair every `pg_window_begin` with
`pg_window_end`.

PureGUI applications must currently be compiled with `-mgeneral-regs-only`.
The kernel does not yet initialize and context-switch SIMD/FPU state for ring-3
processes; emitting SSE instructions otherwise terminates a process with the
`#UD` exception.

## Widgets contract

| Function | Contract |
|---|---|
| `pg_label` | Draws theme-colored text in the client area. |
| `pg_panel` | Draws a themed panel in the supplied client rectangle. |
| `pg_button` | Draws hover/pressed state and returns `true` for a primary-button release inside its rectangle. |

## Current limitations

- Up to eight registered PureGUI windows can coexist; one receives keyboard
  focus at a time.
- Window movement coordinates with the desktop redraw contract, preserving
  the top bar, icons, native windows and overlays.
- Minimize currently collapses a window to its title bar; a system taskbar is
  not implemented yet.
- There are no off-screen compositor surfaces; restoration uses ordered
  application repaint events.
- Mouse and keyboard events come from the current system input syscalls.
- Text uses the fixed-width kernel font.
