# macOS Build

VES supports macOS Apple Silicon (`arm64`) and macOS Intel (`x86_64`) builds.

## Requirements

- macOS with Xcode and command-line tools.
- JUCE/Projucer compatible with JUCE 8.0.13.
- CMake for building SDL3 when a prebuilt static SDL3 prefix is not supplied.
- `jq` for validation performed by the release script.
- `ripgrep` (`rg`) for validation performed by the release script (`brew install ripgrep`).
- Patched MAME 0.289 source under `validation/mame-0.289-patched`.

Both macOS architectures use deployment target macOS 11.0 and static SDL3 from the pinned in-tree SDL source.

## Build MAME Artifacts

Use the macOS release script for normal builds:

```sh
platform/macos/build-macos-release.sh --arch arm64
platform/macos/build-macos-release.sh --arch x86_64
```

On a clean checkout, the script generates the required MAME project files from
tracked source before invoking the generated solution-level Makefile.
Do not invoke `mamevesembedded.make` directly for this step. The solution Makefile builds the selected objects and dependent archives required by the final link.

Intel MAME output must be isolated from Apple Silicon output. The release script uses `BUILDDIR=build-macos-x86_64`, `PLATFORM=x86`, `PTR64=1`, `USE_LIBSDL=1`, `PKG_CONFIG_PATH="$VES_SDL3_PREFIX/lib/pkgconfig"`, and explicit x86_64 architecture options:

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

## SDL3

macOS builds use SDL 3.4.14 from `third_party/sdl3-src`, commit `147a8ee32dbf9ac02f3794964490687b6bbda1bc`.

The generated Xcode project and portable `.jucer` refer to `$(VES_SDL3_PREFIX)/lib/libSDL3.a`. Provide `VES_SDL3_PREFIX` when building. If that prefix does not already contain `lib/libSDL3.a`, `platform/macos/build-macos-release.sh` can build SDL3 from `VES_SDL3_SOURCE_DIR`:

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

Do not link release products against a Homebrew SDL3 dylib.

## VES Build

Run the macOS release build script from the repository root:

```sh
platform/macos/build-macos-release.sh --arch arm64
platform/macos/build-macos-release.sh --arch x86_64
```

`arm64` is the default when `--arch` is omitted.

The script stages products under `Builds/MacOSX/release-validation` for Apple Silicon and `Builds/MacOSX-Intel/release-validation-x86_64` for Intel unless `VES_STAGE_DIR` is set to another disposable output directory. It validates architecture, deployment metadata, MAME linker inputs, selected machine symbols, artwork/plugin resources, VST3 JSON, prohibited media, and SDL linkage.

The portable `Project/VintageEmulatorStudio.jucer` declares `macOSDeploymentTarget="11.0"`. Until the Xcode project is regenerated, pass `MACOSX_DEPLOYMENT_TARGET=11.0` as an Xcode command-line build setting when building manually.
