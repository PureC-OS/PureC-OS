# ADR-008: Ring-3 PureGUI file manager

## Status

Accepted

## Context

The original file manager was a C++ desktop component linked directly into
`kernel-limine.elf`. It owned window drawing, input, path handling and VFS
operations in one implementation. This made a desktop application part of the
trusted kernel image and prevented it from using the same PureGUI contract as
the terminal and third-party applications.

The replacement also needs a clearer representation of folders, files and
physical storage. The current VFS exposes one mounted root; detected disks that
are not selected by the filesystem cannot honestly be presented as browsable
drive letters.

## Decision

Replace the embedded explorer with `/bin/program/files`, a freestanding ring-3
PureGUI V1.0.1 application. The desktop Files icon starts this executable and
the former explorer object is removed from the kernel link.

Split the application into modules for event orchestration, path operations,
filesystem/storage models and presentation. Extend `libpurec` with wrappers for
the existing directory-list, create, delete, rename and move syscalls; no new
privileged filesystem policy is added.

Use an Explorer-style layout with a command bar, address bar, navigation pane,
detailed item rows and status bar. “This PC” displays model, transport,
capacity, online state and mount status for every detected device. Only the
selected operational system disk opens `/`; other devices remain visible as
detected but not mounted.

Package the executable as a boot module and copy and verify it as part of the
installer program payload.

## Consequences

### Positive

- The kernel image no longer contains file-manager UI or interaction policy.
- Files, folders and disks have distinct, information-rich representations.
- File operations cross the existing checked syscall boundary.
- The application inherits PureGUI window movement, minimize, close and
  desktop-scene restoration behavior.
- The modular source layout allows views or mounting support to evolve without
  rewriting navigation and VFS operations.

### Negative

- The FAT directory ABI still limits displayed names to 12 characters.
- One foreground ring-3 GUI application remains the current desktop model.
- Non-selected disks cannot be browsed until the VFS gains mount points.

### Neutral

- The Files desktop icon remains a kernel desktop component, but it only
  launches the ring-3 application.
- Directory rows are paged because the mouse ABI does not expose a wheel.

## Alternatives Considered

**Restyle the embedded explorer** was rejected because presentation code would
remain privileged and duplicate PureGUI behavior.

**Expose every detected disk as a fake drive** was rejected because device
detection does not imply a mounted readable filesystem.

**Introduce a full Windows-compatible shell** was rejected because the current
goal is a small native manager, not Windows API compatibility.

## References

- `docs/adr-006-puregui-system-library.md`
- `docs/adr-007-puregui-desktop-redraw-contract.md`
- `src/programs/files`
- `src/libc/include/purec.h`
