# ADR-002: Add VFS Boundary and Linked Init Process

## Status
Superseded by ADR-003

## Context
The syscall layer used FAT32 directly for file operations. That made the public
filesystem ABI depend on one concrete on-disk filesystem and blocked future
third-party programs from using a stable Linux-like descriptor model.

Kernel startup also jumped directly into linked userspace code from
`kernel_main`, leaving no formal init-process boundary.

## Decision
Introduce `src/fs/vfs.*` as the kernel filesystem boundary. System calls use
VFS functions, while FAT32 is only the current root filesystem backend.

Descriptors returned to userspace are VFS descriptors starting at 3. The VFS
keeps stdin/stdout/stderr reserved as 0, 1 and 2. `read()` no longer closes a
file implicitly at EOF; callers must use `close()`.

Add a small kernel-owned virtual filesystem under `/kernel` with files:
- `/kernel/version`
- `/kernel/init`
- `/kernel/abi`

Introduce `src/kernel/init.*` as the first init boundary. For now it starts the
linked userspace implementation. Later it can load `/sbin/init` or another ELF
binary through the same VFS API.

## Consequences
Positive:
- `kernel/syscall.c` is no longer coupled to FAT32 operations.
- Userspace can target `fs_open`, `fs_read`, `fs_close` and friends instead of
  concrete filesystem internals.
- `/kernel` exposes minimal kernel-provided metadata without requiring a disk.
- The future ELF loader has a clear place to start `/sbin/init`.

Negative:
- FAT32 still contains the real on-disk implementation.
- VFS currently has one disk backend and one read-only virtual backend.
- User pointer validation and per-process descriptor tables are still future
  work.

## Follow-up
ADR-003 added process-owned descriptor tables plus `SYS_EXEC` and `SYS_WAIT`.
The remaining work is `SYS_STAT`, `SYS_LSEEK`, loading `/sbin/init` from VFS,
and an IPC transport before the filesystem backend can leave ring 0.
