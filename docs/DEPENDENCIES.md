# Dependencies

This document records source and build dependencies that are relevant to the VES 0.9.289 source baseline.

## MAME

VES vendors a patched MAME source tree under `validation/mame-0.289-patched`.

- Version: MAME 0.289
- Upstream tag: `mame0289`
- Upstream commit: `f34f02505e32c1993c6a782b6814232cbfc74e36`

MAME license and notice files are retained inside the vendored source tree, including `validation/mame-0.289-patched/COPYING` and `validation/mame-0.289-patched/docs/legal/`.

## JUCE

The VES host project is built with JUCE project files.  The validated project files were produced with JUCE 8.0.13.  Developers must provide a compatible JUCE checkout and Projucer installation for project regeneration.

JUCE modules are not vendored as a complete third-party source tree in this repository.

## SDL

macOS builds use pinned SDL3 source from `third_party/sdl3-src`.

- SDL version: 3.4.14
- Upstream repository: `https://github.com/libsdl-org/SDL`
- Commit: `147a8ee32dbf9ac02f3794964490687b6bbda1bc`
- Observed tag: `release-3.4.14`
- macOS deployment target: 11.0

macOS release builds link SDL3 statically through `$(VES_SDL3_PREFIX)/lib/libSDL3.a`.  Do not commit SDL build/install output or generated `libSDL3.a`.

Linux builds use distribution SDL2 and SDL2_ttf packages for the MAME OSD payload.  See `docs/BUILD_LINUX.md`.

## Fonts And Resources

The Inter font files in `Resources/Fonts/` are accompanied by `Resources/Fonts/OFL.txt`.

VES artwork and media-icon resources are stored under `artwork/` and `Resources/`.  ROMs, firmware, NVRAM, user media, and copyrighted machine media are not distributed in this repository.
