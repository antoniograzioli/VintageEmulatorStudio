# MAME Patchset

Vintage Emulator Studio vendors a patched MAME 0.289 source tree under
`validation/mame-0.289-patched`.

Upstream baseline:

- MAME version: 0.289
- Upstream tag: `mame0289`
- Upstream commit: `f34f02505e32c1993c6a782b6814232cbfc74e36`

The ordered VES patch series is `patches/mame-0.289/series`.  To reproduce the patched tree, apply the series to a clean official MAME 0.289 checkout and then run `scripts/check-ves-mame-patches.sh <tree>`.

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
- Preserves the CZ-101 power-default behavior used by the VES build:
  `m_power` starts on and the `Power` input defaults to on.
- Preserves the 4096 render/capture clamp in the embedded engine and
  VES host UI.
- Preserves Windows MSVC DRC portability changes replacing null-pointer
  integer casts with `std::uintptr_t(0)`/`uintptr_t(0)` forms in the patched
  MAME source.
- Preserves Windows-specific Genie and third-party build configuration changes
  needed by the validated MSVC build, including SDL-related Windows options and
  warning/runtime compatibility settings.

## Embedded Runtime Notes

The embedded runtime is implemented under `src/ves/embeddedinstruments/` in the patched MAME tree.  It runs a selected MAME driver without the normal desktop UI stack, exposes the host-facing `EmbeddedEmulatorEngine`, and uses VES memory bridges for audio, MIDI, video frames, pointer/input delivery, startup media options, and runtime diagnostics.

The runtime launches MAME with VES-controlled audio/MIDI paths.  PortAudio, PortMidi, SDL, or platform libraries may still be linked where required by a target, but they are not the VES plug-in hardware-provider path unless the platform build documentation explicitly says so.

## Patch Areas

- `0001-ves-embedded-target-and-build.patch` adds the `vesembedded` target and selected dependency graph.
- `0002-ves-embedded-runtime-and-frontend.patch` adds the embedded runtime, host API, selected machine registry, and startup media handling.
- `0003-ves-layout-plugin-lua-bindings.patch` exposes the limited layout, render, and Lua bindings used by VES resources.
- `0010` through `0014` add virtual MIDI retrofit paths for selected instruments.
- `0020` through `0025` add or complete selected Yamaha TG100 and Akai sampler profiles.

## Build-System Caveats

Some source-bearing MAME directories look like generated-output directories but are required inputs:

- `validation/mame-0.289-patched/scripts/build/`
- `validation/mame-0.289-patched/3rdparty/softfloat3/build/MAME/platform.h`

Do not remove or ignore those paths when preparing source packages.  Generated MAME project files, compiler output, archives, executables, local GENie bootstrap binaries, ROMs, NVRAM, and user media are not part of the source distribution.

## Reproduction

Use the helper script against an official MAME 0.289 checkout:

```sh
scripts/apply-ves-mame-patches.sh /path/to/mame0289 patches/mame-0.289
scripts/check-ves-mame-patches.sh /path/to/mame0289
```

Maintain the patch series by regenerating it from a clean official MAME 0.289 tree after any MAME update or VES MAME integration change.
