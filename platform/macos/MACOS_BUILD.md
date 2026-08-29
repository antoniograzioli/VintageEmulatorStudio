# macOS release validation

`platform/macos/build-macos-release.sh` supports staged release validation for both canonical macOS architectures:

```sh
platform/macos/build-macos-release.sh --arch arm64
platform/macos/build-macos-release.sh --arch x86_64
```

`arm64` remains the default when `--arch` is omitted. Both architectures use deployment target macOS 11.0 and static SDL3 from the pinned in-tree source.

Use `MACOSX_DEPLOYMENT_TARGET=11.0` for the complete Apple Silicon MAME solution-level build:

```sh
MACOSX_DEPLOYMENT_TARGET=11.0 make -C validation/mame-0.289-patched/build/projects/sdl3/mamevesembedded/gmake-osx-clang config=release64 -j2 all
```

The generated `mamevesembedded.make` must not be called directly. The solution Makefile builds the selected objects and every dependent archive.

The portable `Project/VintageEmulatorStudio.jucer` declares `macOSDeploymentTarget="11.0"`. Until the Xcode project is next regenerated, pass `MACOSX_DEPLOYMENT_TARGET=11.0` as an Xcode command-line build setting; exporting it only as an environment variable does not override the generated per-target setting.

Intel x86_64 MAME output must be isolated from the validated Apple Silicon payload. The canonical script uses `BUILDDIR=build-macos-x86_64`, `PLATFORM=x86`, `PTR64=1`, `USE_LIBSDL=1`, `PKG_CONFIG_PATH="$VES_SDL3_PREFIX/lib/pkgconfig"`, and explicit `-arch x86_64 -mmacosx-version-min=11.0` architecture options:

```sh
PKG_CONFIG_PATH="$VES_SDL3_PREFIX/lib/pkgconfig" MACOSX_DEPLOYMENT_TARGET=11.0 \
make REGENIE=1 SUBTARGET=vesembedded OSD=sdl3 TARGETOS=macosx PLATFORM=x86 PTR64=1 USE_LIBSDL=1 \
  BUILDDIR=build-macos-x86_64 \
  ARCHOPTS="-arch x86_64 -mmacosx-version-min=11.0" \
  ARCHOPTS_C="-arch x86_64 -mmacosx-version-min=11.0" \
  ARCHOPTS_CXX="-arch x86_64 -mmacosx-version-min=11.0" \
  ARCHOPTS_OBJC="-arch x86_64 -mmacosx-version-min=11.0" \
  ARCHOPTS_OBJCXX="-arch x86_64 -mmacosx-version-min=11.0" \
  -j2
```

Run `platform/macos/build-macos-release.sh` for staged release validation. It requires all 71 MAME linker inputs, verifies their 11.0 deployment metadata, builds Standalone/AU/VST3, checks the selected architecture, all 45 machine symbols, 37 artwork plus 3 plugin resource files, valid VST3 JSON, prohibited media, stale references, and RC-tree dependencies. Generated Xcode install phases are disabled in the canonical project; validation must never target the real user plugin folders.

By default the script stages Apple Silicon inside `Builds/MacOSX/release-validation` and Intel inside `Builds/MacOSX-Intel/release-validation-x86_64`. Set `VES_STAGE_DIR` to an empty disposable directory on a volume with sufficient free space when necessary; this changes only validation output, never the project or installed plugin locations.

SDL3 is release-critical and must not be resolved from Homebrew at link or runtime. The canonical policy is static linkage against SDL 3.4.14, commit `147a8ee32dbf9ac02f3794964490687b6bbda1bc`, built per architecture with `CMAKE_OSX_DEPLOYMENT_TARGET=11.0`.

The generated Xcode project and portable `.jucer` refer to `$(VES_SDL3_PREFIX)/lib/libSDL3.a`. Provide `VES_SDL3_PREFIX` when building. If that prefix does not already contain `lib/libSDL3.a`, `platform/macos/build-macos-release.sh` can build it from `VES_SDL3_SOURCE_DIR` using:

```sh
cmake -S "$VES_SDL3_SOURCE_DIR" -B "$VES_SDL3_BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="$arch" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_INSTALL_PREFIX="$VES_SDL3_PREFIX" \
  -DSDL_SHARED=OFF \
  -DSDL_STATIC=ON \
  -DSDL_TESTS=OFF \
  -DSDL_EXAMPLES=OFF \
  -DSDL_INSTALL_TESTS=OFF \
  -DSDL_INSTALL_DOCS=OFF
```

For Intel, the script derives or accepts `JUCE_MODULES_DIR` and rewrites the generated `Builds/MacOSX-Intel` project to use `$(JUCE_MODULES_DIR)` instead of embedding a user-specific JUCE module path. Do not copy Intel RC `.jucer` files or temporary staging paths into the canonical tree.

The script verifies the pinned SDL commit, archive minOS, required SDL symbols, and final products. It rejects bare `-lSDL3`, `/opt/homebrew` SDL linker paths, `libSDL3.0.dylib` runtime dependencies, wrong architecture, invalid VST3 JSON, incomplete MAME payloads, and plugin installation into real user folders.
