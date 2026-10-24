# ADR-0001: ACPI namespace is the source for devices and thermal telemetry

## Status

Accepted

## Context

The kernel already loads and initializes the AML namespace through uACPI. The
old device manager guessed devices from ACPI table signatures, which cannot
describe the firmware's actual device tree. Temperature and fan telemetry are
also AML methods rather than fixed ACPI table fields.

## Decision

Enumerate ACPI devices from the initialized AML namespace and collect `_HID`,
`_UID`, `_ADR`, and `_STA`. Read thermal zones with `_TMP` (tenths Kelvin) and
fan speed with `_FST` (RPM), exporting a read-only `SYS_THERMAL_INFO` syscall.

## Consequences

The Device Manager reports only firmware-described devices and telemetry.
Systems whose firmware does not publish `_TMP` or `_FST` correctly show the
value as unavailable. This does not access Intel MSRs or implement fan control,
so it remains safe on laptops and desktops with vendor-specific EC protocols.
