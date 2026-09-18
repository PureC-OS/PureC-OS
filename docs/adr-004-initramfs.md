# ADR-004: Load boot assets from initramfs

## Status

Accepted

## Context

Each program, library and resource in `assets/manifest.txt` used to be passed
to the kernel as a separate Limine module. This made the boot configuration
large and tied program loading to Limine's module response.

## Decision

`mk/gen_assets.py` creates a CPIO `newc` archive at
`/boot/initramfs.cpio`. Limine loads that archive as the sole asset module.
The kernel mounts it read-only through VFS before starting PID 1. ELF loading
therefore uses the normal VFS path. `boot_get_module()` retains a read-only
fallback into the archive for the installer during the transition.

The installer writes the same archive to `/boot/initramfs.cpio` and its Limine
configuration loads it for both primary and fallback entries.

## Consequences

The initial userspace is independent of a discovered disk filesystem, and the
Limine configuration has one asset module. Initramfs contents are immutable;
writes still require a mounted FAT32 or ext2 root filesystem.
