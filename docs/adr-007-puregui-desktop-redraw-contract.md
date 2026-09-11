# ADR-007: PureGUI desktop redraw contract

## Status

Accepted

## Context

PureGUI applications render directly into the shared framebuffer. Clearing a
window's previous rectangle with the desktop color destroys pixels owned by
the top bar, desktop icons, native windows and overlays. A correct move,
minimize, close or return from a full-screen child must restore the current
desktop scene before the external window is drawn again.

The current system supports one foreground ring-3 GUI application and has no
compositor or shared application surfaces.

## Decision

Add the `SYS_DESKTOP_REDRAW` coordination syscall and expose it to ring-3 as
`pc_desktop_redraw()`. The syscall queues a generation-numbered request and
waits while the desktop thread reconstructs its scene in its established
z-order. Drawing in the desktop thread is required because its text and metric
renderers use kernel-owned buffers that must not be validated as buffers of the
requesting ring-3 process. PureGUI invokes this contract before moving or
closing a window and while collapsing it. The terminal also invokes it before
restoring itself after a child process that used the display.

PureGUI remains responsible only for its own window chrome and contents. It no
longer guesses which color or system components occupied the old rectangle.

## Consequences

### Positive

- Moving a PureGUI window preserves the top bar, icons and native windows.
- Restoration always uses current desktop state instead of stale pixels.
- Kernel buffer validation remains enabled during redraw coordination.
- The contract can later be implemented by a compositor without changing the
  PureGUI application API.

### Negative

- A move redraws the full desktop and can cost more than dirty-rectangle
  composition.
- The requesting application can wait up to one desktop polling interval.
- Ring-3 windows are restored in z-order through the window registry, but they
  still draw directly rather than owning compositor surfaces.

### Neutral

- The terminal's retained character grid remains responsible for restoring
  terminal text after the desktop scene is rebuilt.

## Alternatives Considered

**Fill the old rectangle with the desktop color** was rejected because it
erases system-owned pixels, which caused the reported gray shell and missing
icons.

**Save and restore framebuffer pixels** was rejected because snapshots become
stale when clocks, audio overlays or native windows change behind the client.

**Introduce a compositor immediately** was deferred because shared surfaces,
z-order ownership and routed input are larger than this compatibility fix.

## References

- `docs/adr-006-puregui-system-library.md`
- `src/userspace/userspace.c`
- `src/libgui/window.c`
