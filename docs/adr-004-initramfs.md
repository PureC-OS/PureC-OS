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

For both FAT32 and ext2 installations, the installer writes the archive to the
FAT32 EFI system partition (ESP), with long name `/boot/initramfs.cpio` and
short alias `/boot/initra~1.cpi`. Installed Limine entries use the short alias;
ISO entries use the long name. The kernel and installer share the
`initramfs_boot_image()` lookup accepting both names, including the uppercase
short alias. Installation verifies the exact alias used by Limine. Mounting
the archive does not require the disk root filesystem to be mounted first.

## Consequences

The initial userspace is independent of a discovered disk filesystem, and the
Limine configuration has one asset module. Initramfs contents are immutable;
writes still require a mounted FAT32 or ext2 root filesystem.
