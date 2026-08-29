# SDL3 Pin

Canonical macOS release builds use SDL 3.4.14 as a static library for both Apple Silicon and Intel.

- Repository: `https://github.com/libsdl-org/SDL`
- Commit: `147a8ee32dbf9ac02f3794964490687b6bbda1bc`
- Version tag observed in the pinned checkout: `release-3.4.14`
- Build product: `libSDL3.a`
- Architectures: `arm64`, `x86_64`
- Deployment target: `MACOSX_DEPLOYMENT_TARGET=11.0`

The canonical release script accepts the SDL source checkout through
`VES_SDL3_SOURCE_DIR` and the install prefix through `VES_SDL3_PREFIX`.
When `VES_SDL3_SOURCE_DIR` is not set, it defaults to the pinned in-tree
source at `third_party/sdl3-src`.

Use architecture-specific build and install directories. The validated defaults are under:

- `Builds/MacOSX/release-validation/deps/sdl3-arm64-macos11`
- `Builds/MacOSX-Intel/release-validation-x86_64/deps/sdl3-x86_64-macos11`

`third_party/sdl3-src/.canonical-source-commit` records the pinned commit for
source-only distributions that intentionally omit the upstream `.git`
directory. Do not package SDL build/install directories or `libSDL3.a` as
source; those remain generated release artifacts.
