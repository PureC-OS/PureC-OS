# ADR-001: Move UI Access Behind the System API

## Status
Accepted

## Context
The current kernel image contains desktop, terminal, apps and drawing code.
Those modules used the GOP driver directly, which made framebuffer rendering a
kernel-internal dependency of higher-level UI code.

## Decision
Keep GOP/framebuffer support in the kernel only as a low-level display driver
for boot diagnostics, kernel panic output and system API implementation.
Expose framebuffer information and drawing operations through syscall numbers
`SYS_FB_INFO`, `SYS_DRAW_TEXT`, `SYS_DRAW_TEXT_SIZED`,
`SYS_SCROLL_RECT_UP`, `SYS_SET_FONT_FACE` and `SYS_GET_FONT_FACE`.

Userspace modules must call `src/userspace/display.*` and
Standalone ring-3 programs use `libpurec` monitoring calls instead of including
GOP, PMM, scheduler, or system-info internals.

## Consequences
Positive:
- UI code no longer depends on the GOP driver header.
- The framebuffer remains available without making graphics a desktop concern
  inside core kernel code.
- A future ring3 userspace loader can keep the same API boundary.

Negative:
- The UI code is still linked into the kernel image until process/address-space
  loading exists.
- The syscall layer currently trusts pointers because this OS does not yet have
  user pointer validation.

## Follow-up
Move keyboard, mouse, filesystem and power access behind similar userspace API
modules. After that, introduce a small ELF/program loader and stop linking
`src/userspace` objects into `kernel-limine.elf`.
