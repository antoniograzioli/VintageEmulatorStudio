# Source Tree Notes

## Intentional exclusions

This tree excludes `Dist/`, release/build outputs, generated MAME projects, object files, libraries, executables, caches, ROMs, samples/test media, NVRAM, `.git` directories, rollback and validation snapshots, `.local/`, `working/`, logs, `.DS_Store`, AppleDouble files, Photoshop source artwork, and machine-local dependency build trees.

Do not apply a blanket exclusion to directories named `build`: MAME's `scripts/build/` and SoftFloat's `3rdparty/softfloat3/build/MAME/platform.h` are required source/build-system inputs. Generated MAME output directories remain excluded from distributable source.

## Portability caveats

The retained MAME and JUCE integration files intentionally describe links to artifacts that must be generated in a later build-validation phase.  No build artifacts are shipped here.  `Project/VintageEmulatorStudio.jucer` is a cleaned base Xcode definition, not a verified universal exporter definition: the Windows, Linux, and Intel exporter references are preserved separately under `platform/`.  A future project-definition consolidation must be validated by builds on all four targets.

The canonical embedded MAME engine uses the base/Linux/Windows 4096 capture/render clamp.  The Intel-only 2048 variation is intentionally not included.  The Windows MAME DRC `uintptr_t(0)` compiler-compatibility edits and Windows Genie configuration are included.

## Relationship to release candidates

This directory was created by copying selected content from the four known-good release-candidate trees.  The following originals remain untouched:

- `VintageEmulatorStudioMAME289`
- `VintageEmulatorStudioMAME289_Intel`
- `VintageEmulatorStudioMAME289_Win`
- `VintageEmulatorStudioMAME289_Linux`

No build, generator, cleanup, Projucer, Make, MSBuild, or Xcode command was run while creating this source tree.

## Apple Silicon MAME build workflow

After MAME generation, build the complete `vesembedded` solution payload from the canonical MAME directory with:

```sh
make -C build/projects/sdl3/mamevesembedded/gmake-osx-clang \
  config=release64 -j2 all
```

This solution-level command is required. Invoking `mamevesembedded.make` directly with `-f` is insufficient because it does not build the required archive projects.

## macOS validation-output caveat

The portable Xcode exporter disables JUCE's plugin copy-after-build step (`enablePluginBinaryCopyStep="0"`) so regenerated projects keep AU and VST3 output in the build/staging location. The currently generated Xcode project predates that setting; regenerate it only in the canonical tree before future validation builds that must not copy bundles to the user's plugin folders.

The canonical generated Xcode project additionally contains no-op AU/VST3 install phases for immediate validation use. The portable `.jucer` retains `enablePluginBinaryCopyStep="0"`, and its macOS deployment target is 11.0. Release builds must pass `MACOSX_DEPLOYMENT_TARGET=11.0` as an Xcode build setting until the project is regenerated.

SDL3 is a pinned macOS release dependency. Apple Silicon and Intel release builds use static SDL 3.4.14 from commit `147a8ee32dbf9ac02f3794964490687b6bbda1bc`, built per architecture with deployment target 11.0 and linked through `$(VES_SDL3_PREFIX)/lib/libSDL3.a`. A developer-local Homebrew SDL3 dylib is not acceptable for release products. The pinned source is physically present at `third_party/sdl3-src`; `VES_SDL3_SOURCE_DIR` remains available only as an explicit override. Future source-packaging and `.gitignore` rules must keep `third_party/sdl3-src/` while excluding SDL build/install outputs and generated `libSDL3.a`.

Intel validation uses isolated generated output under `Builds/MacOSX-Intel/` and `validation/mame-0.289-patched/build-macos-x86_64/`. These generated directories must not overwrite or replace the validated Apple Silicon `Builds/MacOSX/` and `validation/mame-0.289-patched/build/` artifacts.
