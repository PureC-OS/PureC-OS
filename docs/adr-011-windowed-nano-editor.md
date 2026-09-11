# ADR-011: Run nano as a managed PureGUI window

## Status

Accepted

## Context

Nano was a ring-3 process but still used `pc_display_clear()` and blocking
`SYS_GETCHAR` directly. Starting it erased the complete desktop, bypassed the
PureGUI window registry and made the editor appear as a full-screen program.
It also could not participate correctly in focus, movement, minimization or
ordered repaint handling.

## Decision

Give nano a dedicated window adapter built on the existing PureGUI system
library. The adapter owns window registration, frame creation, client-console
configuration and cleanup. Editor rendering is enclosed by the window update
boundary so a repaint is acknowledged only after both the frame and editor
contents have been restored.

Nano consumes keyboard input from `pg_window_poll_event()` instead of the raw
blocking keyboard syscall. Move, focus, minimize and repaint events redraw the
editor inside its client region; passive mouse movement does not trigger a
redraw. Closing the editor disables its client console and restores the desktop.

Keep dynamic text storage in the editor module and window coordination in
`window.c`. This prevents display policy from becoming coupled to buffer and
file operations.

## Consequences

### Positive

- Opening nano no longer clears or owns the complete framebuffer.
- The editor can be moved, minimized and closed through the same controls as
  terminal and file-manager windows.
- Keyboard focus and coordinated repaints use the existing system contract.
- Buffer growth and file I/O remain independent of window management.

### Negative

- Nano now links the PureGUI system library in addition to libc.
- The current editor still renders through a console placed inside the window;
  a future cursor-based editor may need a dedicated text-view widget.

## Alternatives Considered

**Configure a smaller console without registering a window** was rejected
because it would still bypass focus, z-order, dragging and repaint ownership.

**Embed nano into the terminal process** was rejected because applications are
already isolated ring-3 ELF modules and the file manager also launches nano.

## References

- `src/programs/nano/window.c`
- `src/programs/nano/editor.c`
- `src/libgui/event.c`
- `docs/adr-009-puregui-focus-and-repaint.md`
