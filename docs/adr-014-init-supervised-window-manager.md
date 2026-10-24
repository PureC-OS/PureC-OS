# ADR-014: PID 1 supervises the ring-3 window manager

## Status

Accepted

## Context

The kernel started PID 1 but continued to initialize the desktop and create
the desktop input threads itself.  Window policy therefore remained coupled
to ring 0 and PID 1 was only an orphan reaper.

## Decision

The kernel starts only `/bin/init`. PID 1 starts and supervises the required
`/bin/window-manager` and login services. The window manager owns the desktop
event loop in ring 3. Drivers and validation remain in the kernel. A narrow,
capability-checked syscall bridge lets the WM claim the role, route pointer
focus and coordinate desktop redraws with existing PureGUI clients.

The kernel window registry remains temporarily as a compatibility mechanism;
its policy and storage will move behind IPC in the next stage.

## Alternatives considered

- Keep desktop kernel threads and add a nominal WM process: rejected because
  the process would not own any real policy.
- Move every desktop component in one change: rejected because there would be
  no testable boundary between service supervision, IPC and rendering.

## Consequences

- A WM crash is isolated and PID 1 restarts it.
- GUI startup and redraw no longer require a ring-0 desktop polling thread.
- The compatibility registry is still trusted kernel code and must be removed
  after IPC-backed per-process window state is available.
