# MAME Patchset

Vintage Emulator Studio vendors a patched MAME 0.289 source tree under
`validation/mame-0.289-patched`.

Upstream baseline:

- MAME version: 0.289
- Upstream commit: `f34f02505e32c1993c6a782b6814232cbfc74e36`

The ordered canonical patch series is `patches/mame-0.289/series`.  Future
upstream backports or MAME upgrades should update this document and the patch
series together.

## Intentional VES Changes

- Adds the `vesembedded` MAME subtarget and build integration in
  `scripts/target/mame/vesembedded.lua`, `scripts/genie.lua`, and
  `scripts/src/3rdparty.lua`.
- Adds the embedded runtime under `src/ves/embeddedinstruments/`, including
  host-side audio buffering, MIDI routing, video capture, input/event handling,
  runtime diagnostics, and selected-driver startup handling.
- Exposes limited layout/render and plugin/Lua integration needed by the
  embedded host, including MIDI delivery hooks and render-container bindings.
- Selects the VES machine set for the embedded target, including Yamaha,
  Casio, Ensoniq, Akai, Roland, Linn, Oberheim, PAiA, and Sequential profiles
  present in the current `vesembedded` registry.
- Adds VES virtual MIDI retrofit handling for Prophet-5, LinnDrum, Oberheim
  DMX, Yamaha PSR-11, and Yamaha PSR/PSS-family drivers.
- Adds experimental Akai S2000, S3000, S3000XL, CD3000XL, and CD3000i
  embedded-profile integration using the upstream `akai/s3000.cpp` driver
  closure.
- Adds Yamaha TG100 embedded-profile integration and the required selected
  device closure.
- Fixes selected-driver startup image option parsing so Akai floppy media
  options are registered for the requested machine before command-line parsing.
- Preserves the CZ-101 power-default behavior used by the canonical build:
  `m_power` starts on and the `Power` input defaults to on.
- Preserves the canonical 4096 render/capture clamp in the embedded engine and
  VES host UI.
- Preserves Windows MSVC DRC portability changes replacing null-pointer
  integer casts with `std::uintptr_t(0)`/`uintptr_t(0)` forms in the patched
  MAME source.
- Preserves Windows-specific Genie and third-party build configuration changes
  needed by the validated MSVC build, including SDL-related Windows options and
  warning/runtime compatibility settings.

Generated MAME project files, compiler output, archives, executables, and local
GENie bootstrap binaries are not part of the canonical source baseline.
