# Canonical Source Manifest

`VintageEmulatorStudioMAME289_Source` is a source-only consolidation of four release-candidate trees.  The four originals were read-only inputs and remain separate from this tree.

| Canonical section | Origin and reconciliation |
| --- | --- |
| `Source/` | `VintageEmulatorStudioMAME289`, except `VintageEmulatorStudioProcessor.cpp`, which combines macOS, Windows, and Linux packaged-resource resolution and removes the stale MAME 0.288 fallback. |
| `Resources/`, `artwork/`, `JuceLibraryCode/`, `Support/`, `patches/`, `scripts/`, `third_party/`, `docs/` | Base Apple Silicon tree.  The base Six-Trak artwork is the selected version. |
| `validation/mame-0.289-patched/` | Base patched MAME 0.289 source baseline, source-only.  The embedded engine retains the 4096 capture/render clamp. |
| MAME DRC compatibility | Windows copies of `drcbearm64.cpp`, `drcbeut.h`, and `drcbex64.cpp`; they retain `uintptr_t(0)` compatibility edits. |
| MAME Windows configuration | Windows `scripts/genie.lua` and `scripts/src/3rdparty.lua`, retaining MSVC-specific runtime/warning configuration. |
| `Builds/VisualStudio2022/` and `platform/windows/` | Windows tree, excluding `.vs`, platform output folders, and logs. |
| `Builds/LinuxMakefile/` and `platform/linux/` | Linux tree, excluding `build/`; includes `LinuxMameIntegration.mk`, release script, and build documentation. |
| `Builds/MacOSX/` and `platform/macos/` | Base Apple Silicon Xcode configuration with no build products/caches; Intel exporter reference comes from the Intel tree. |
| `Project/VintageEmulatorStudio.jucer` | Base Xcode project definition relocated to `Project/`, with the obvious user-specific JUCE module path replaced by `../JUCE/modules`. |
| macOS SDL3 dependency | `third_party/sdl3-src/`, SDL 3.4.14 source, commit `147a8ee32dbf9ac02f3794964490687b6bbda1bc`, built as static `libSDL3.a` per architecture for `arm64` and `x86_64`/macOS 11 through `platform/macos/build-macos-release.sh` using architecture-specific `VES_SDL3_PREFIX` locations. `VES_SDL3_SOURCE_DIR` may override the source location for controlled external dependency staging. |

## Apple Silicon MAME build procedure

Generate the complete Apple Silicon `vesembedded` payload from the canonical MAME tree with:

```sh
make -C build/projects/sdl3/mamevesembedded/gmake-osx-clang \
  config=release64 -j2 all
```

This invokes the generated solution-level Makefile and produces both the selected driver objects and its dependent MAME, OSD, and third-party archives. Do not invoke `mamevesembedded.make` directly with `-f` for this purpose: that project makefile consumes archive prerequisites but does not build the solution-level prerequisite projects itself.

Apple Silicon release validation uses `platform/macos/build-macos-release.sh --arch arm64`, which is also the default when `--arch` is omitted. It builds MAME with `MACOSX_DEPLOYMENT_TARGET=11.0`, uses a staged Xcode output, normalizes JUCE's VST3 `moduleinfo.json` trailing-comma defect, and validates the result with `jq`.

Intel release validation uses `platform/macos/build-macos-release.sh --arch x86_64`. It preserves the Apple Silicon payload by using an isolated MAME build directory, `validation/mame-0.289-patched/build-macos-x86_64`, and an isolated static SDL3 prefix under `Builds/MacOSX-Intel/release-validation-x86_64/deps/sdl3-x86_64-macos11`.

macOS release products must link SDL3 statically through `$(VES_SDL3_PREFIX)/lib/libSDL3.a`. Do not use a Homebrew SDL3 dylib for release output. The release script validates the static SDL archive and rejects `/opt/homebrew` SDL runtime dependencies.

`third_party/sdl3-src/` is distributable source and must not be excluded by future source-packaging or `.gitignore` rules. Exclude SDL build/install output directories and generated `libSDL3.a`; do not exclude the pinned SDL source tree itself.

`validation/mame-0.289-patched/scripts/build/` is required MAME build-system source. `3rdparty/softfloat3/build/MAME/platform.h` is likewise required third-party source; source packaging rules must not exclude every directory merely because it is named `build`.

The Windows and Linux exporter definitions are retained as platform references under `platform/windows/` and `platform/linux/`.  They are not replacements for a future fully portable multi-exporter project definition.
