# ADR-008: PureAudio modular system library

## Status

Accepted

## Context

PureC OS includes an in-kernel audio subsystem with support for Intel High Definition Audio (HDA) and legacy PC Speaker, controlled via system calls (`SYS_AUDIO_*`). Previously, user applications either performed raw syscalls or relied on an ad-hoc userspace audio helper.

Applications (such as the desktop panel, Settings, media players, and system sounds) need a clean, consistent, freestanding C library to interact with audio and sound devices without directly linking against internal kernel headers or duplicating status/device parsing.

## Decision

Introduce PureAudio as a static system library:

- `/lib/libpureaudio.a` provides volume control, mute management, output device discovery/switching, status reporting, and test playback.
- Public header is `/include/pureaudio.h` (`src/libaudio/include/pureaudio.h`).

The API begins at `PA_API_VERSION 1` (release version `V1.0.0`).
Like `libpuregui` and `libpurefs`, it is compiled with `-mgeneral-regs-only` and has no heap dependencies.

## Consequences

### Positive

- Standardized and consistent audio API for all ring-3 applications.
- Easy volume adjustments, mute toggles, and device switching with helper functions (`pa_volume_up`, `pa_volume_down`, `pa_next_output_device`, `pa_backend_name`).
- Automatically packaged into `/lib` and `/include` inside the ISO distribution.
- Settings app and other user tools link against `libpureaudio.a` cleanly.

### Negative

- Static linking duplicates a few hundred bytes of code in applications that link it.
