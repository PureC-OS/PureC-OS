# ADR-006: PureGUI modular system library

## Status

Accepted

## Context

Ring-3 applications currently call framebuffer syscalls directly. Repeating
window chrome, client-coordinate translation, input polling and widget drawing
in every application makes the UI inconsistent and couples applications to the
current framebuffer ABI. Porting Qt would add a runtime, allocator and platform
surface far larger than the operating system currently needs.

The first release must remain freestanding, avoid dynamic allocation, run on
the existing syscall ABI and be usable from small C programs.

## Decision

Introduce PureGUI as two static system libraries:

- `/lib/libpuregui.a` provides themes, foreground windows, clipped drawing and
  normalized keyboard/mouse events.
- `/lib/libpguiw.a` provides labels, panels and buttons on top of the
  core library.

Public headers live under `/include`. Applications use client-relative
coordinates and link only the layers they need. The ABI begins at
`PG_API_VERSION 1`; the current ABI is `PG_API_VERSION 3`, while the packaged
library release uses the semantic version `V1.0.2`.

PureGUI routes keyboard input to one focused window while allowing multiple
registered ring-3 windows. Version 2 added title-bar collapse/restore and
version 3 adds focus and coordinated repaint events.
Rendering still uses the existing framebuffer syscalls; the public API does
not expose that backend so a future compositor can replace it without changing
application source.

## Consequences

### Positive

- Applications no longer implement window frames and input normalization.
- Core and widgets are separately linkable and contain no heap dependency.
- A future window server can preserve the application-facing API.

### Negative

- Windows use coordinated repaint rather than independent composited surfaces.
- Static linking duplicates the small client runtime in each application.
- Text uses the current fixed-width system font.

### Neutral

- `/bin/gui-demo` acts as executable API documentation.
- PureGUI API version 2 adds close/minimize window controls and normalized
  minimize events without introducing a window-server dependency.
- The packaged library version is `V1.0.2`; it adds managed focus and ordered
  restoration of multiple ring-3 windows.
- ADR-007 replaces solid-color cleanup with desktop-owned scene restoration.
- PureGUI and its applications are compiled with `-mgeneral-regs-only` until
  the kernel provides per-process SIMD/FPU context management.

## Alternatives Considered

**Port Qt** was rejected because its platform abstraction, event loop and
runtime dependencies are disproportionate to the current kernel.

**Put widgets in the kernel** was rejected because widget policy does not need
kernel privilege and would make the syscall ABI difficult to evolve.

**Build a compositor first** was deferred until processes can share surfaces
and receive routed input events.
