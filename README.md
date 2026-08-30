# Vintage Emulator Studio

Vintage Emulator Studio is an instrument plug-in project for vintage synthesizers, drum machines, sound modules, and samplers.  The current source baseline is VES 0.9.289 with an embedded MAME 0.289 source baseline.

This repository contains the VES host code, artwork/layout resources, JUCE project files, platform build integration, a patched MAME 0.289 source tree, and a reproducible patch series describing the VES modifications to MAME.

## Status

VES 0.9.289 is a source baseline for four supported build configurations:

- macOS Apple Silicon (`arm64`)
- macOS Intel (`x86_64`)
- Windows x64
- Linux x86_64

The repository does not contain release binaries or generated build output.

## Architecture

VES is a JUCE-based audio plug-in/application that embeds a selected MAME target.  The VES MAME integration provides a reduced `vesembedded` target, an embedded runtime, in-memory audio and MIDI bridges, video capture for plug-in rendering, selected machine profiles, and virtual MIDI retrofit paths for supported instruments.

MAME-derived code is vendored under `validation/mame-0.289-patched`.  The upstream MAME baseline is 0.289 at commit `f34f02505e32c1993c6a782b6814232cbfc74e36`.

## Repository Layout

- `Source/` - VES JUCE processor/editor and host integration code.
- `Resources/` - VES plug-in resources, fonts, media icons, logo, and MAME plug-in scripts.
- `artwork/` - VES artwork and MAME layout files used by supported instruments.
- `Project/` - portable JUCE project definition.
- `Builds/` - checked-in JUCE-generated project files used by the supported platform workflows.
- `platform/` - platform-specific project definitions and release build scripts.
- `validation/mame-0.289-patched/` - patched MAME 0.289 source used by VES.
- `patches/mame-0.289/` - ordered patch series for reproducing the VES MAME modifications.
- `third_party/sdl3-src/` - pinned SDL3 source used by macOS builds.
- `docs/` - public build, dependency, source-layout, and MAME patch documentation.

## Build Documentation

- macOS: `docs/BUILD_MACOS.md`
- Windows: `docs/BUILD_WINDOWS.md`
- Linux: `docs/BUILD_LINUX.md`

MAME patch details are documented in `docs/MAME_PATCHSET.md`.

## ROMs, Firmware, And Machine Media

ROMs, firmware, NVRAM, user media, installed plug-ins, and build products are intentionally excluded from source control.  Users and developers must obtain any required machine data themselves and configure it locally through the plug-in's ROM/media settings.

Do not add ROMs, firmware, NVRAM, personal machine state, or copyrighted machine media to this repository.

## Attribution

VES uses MAME-derived emulation code where required by the applicable upstream licensing and attribution terms.  MAME is not part of the product name, and this project does not claim endorsement by the upstream MAME project.

Third-party license and notice files are retained with their respective source trees and resources.  A VES-specific project license has not yet been selected for public release.
