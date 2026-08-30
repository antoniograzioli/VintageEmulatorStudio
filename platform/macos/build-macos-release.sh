#!/bin/sh
set -eu

usage() {
    echo "Usage: $0 [--arch arm64|x86_64]" >&2
    exit 2
}

arch=arm64
while [ "$#" -gt 0 ]; do
    case "$1" in
        --arch)
            [ "$#" -ge 2 ] || usage
            arch=$2
            shift 2
            ;;
        --arch=*)
            arch=${1#--arch=}
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            usage
            ;;
    esac
done

case "$arch" in
    arm64)
        mame_builddir=build
        xcode_dir_name=MacOSX
        default_stage_suffix=release-validation
        excluded_archs="i386 x86_64 arm64e"
        sdl3_default_suffix=sdl3-arm64-macos11
        ;;
    x86_64)
        mame_builddir=build-macos-x86_64
        xcode_dir_name=MacOSX-Intel
        default_stage_suffix=release-validation-x86_64
        excluded_archs="arm64 arm64e i386"
        sdl3_default_suffix=sdl3-x86_64-macos11
        ;;
    *)
        usage
        ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mame="$root/validation/mame-0.289-patched"
source_xcode_dir="$root/Builds/MacOSX"
xcode_dir="$root/Builds/$xcode_dir_name"
project="$xcode_dir/Vintage Emulator Studio.xcodeproj"
stage=${VES_STAGE_DIR:-"$xcode_dir/$default_stage_suffix"}
derived_data=${VES_DERIVED_DATA_DIR:-"$stage/DerivedData"}
sdl3_source=${VES_SDL3_SOURCE_DIR:-"$root/third_party/sdl3-src"}
sdl3_prefix=${VES_SDL3_PREFIX:-"$stage/deps/$sdl3_default_suffix"}
sdl3_build=${VES_SDL3_BUILD_DIR:-"$stage/BuildDeps/$sdl3_default_suffix-build"}
sdl3_commit=147a8ee32dbf9ac02f3794964490687b6bbda1bc
juce_modules_dir=${JUCE_MODULES_DIR:-}

test -d "$mame"
test -x "$root/platform/macos/normalize-vst3-moduleinfo.sh"

work=$(mktemp -d "${TMPDIR:-/tmp}/ves-macos-release-check.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

assert_minos_11_archive_or_binary() {
    target=$1
    otool -lv "$target" | awk '/minos/ { if ($2 != "11.0") exit 1 }'
}

assert_arch_archive_or_binary() {
    target=$1
    case "$target" in
        *.a)
            otool -hv "$target" | awk -v arch="$arch" '
                /Mach header/ { next }
                /X86_64|ARM64/ {
                    seen=1
                    if (arch == "x86_64" && $2 != "X86_64") exit 1
                    if (arch == "arm64" && $2 != "ARM64") exit 1
                }
                END { if (!seen) exit 1 }
            '
            ;;
        *)
            file "$target" | grep -q "$arch"
            ;;
    esac
}

assert_no_homebrew_sdl_runtime() {
    binary=$1
    ! otool -L "$binary" | rg '/opt/homebrew.*/libSDL3|libSDL3\.0\.dylib'
    ! otool -L "$binary" | rg 'libSDL3\.0\.dylib'
}

assert_no_plugin_install_phase() {
    pbxproj=$1
    test "$(rg -c 'Plugin install disabled for canonical validation' "$pbxproj")" = 2
    ! rg -q 'ditto.*BUILT_PRODUCTS_DIR.*destinationPath' "$pbxproj"
}

verify_sdl3_source_pin() {
    test -f "$sdl3_source/CMakeLists.txt"
    test -f "$sdl3_source/.canonical-source-commit"
    test "$(sed -n '1p' "$sdl3_source/.canonical-source-commit")" = "$sdl3_commit"
}

build_sdl3_if_needed() {
    if [ ! -s "$sdl3_prefix/lib/libSDL3.a" ]; then
        verify_sdl3_source_pin
        cmake -S "$sdl3_source" -B "$sdl3_build" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_OSX_ARCHITECTURES="$arch" \
            -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
            -DCMAKE_INSTALL_PREFIX="$sdl3_prefix" \
            -DSDL_SHARED=OFF \
            -DSDL_STATIC=ON \
            -DSDL_TESTS=OFF \
            -DSDL_EXAMPLES=OFF \
            -DSDL_INSTALL_TESTS=OFF \
            -DSDL_INSTALL_DOCS=OFF
        cmake --build "$sdl3_build" --config Release -j 2
        cmake --install "$sdl3_build" --prefix "$sdl3_prefix"
    fi

    test -s "$sdl3_prefix/lib/libSDL3.a"
    file "$sdl3_prefix/lib/libSDL3.a" | grep -q 'current ar archive'
    assert_arch_archive_or_binary "$sdl3_prefix/lib/libSDL3.a"
    assert_minos_11_archive_or_binary "$sdl3_prefix/lib/libSDL3.a"
    nm -gU "$sdl3_prefix/lib/libSDL3.a" | rg -q '_SDL_Init$'
    nm -gU "$sdl3_prefix/lib/libSDL3.a" | rg -q '_SDL_OpenAudioDevice'
}

prepare_xcode_project() {
    if [ "$arch" = arm64 ]; then
        test -f "$project/project.pbxproj"
        return
    fi

    mkdir -p "$xcode_dir"
    cp "$source_xcode_dir"/Info-*.plist "$xcode_dir"/
    cp "$source_xcode_dir/RecentFilesMenuTemplate.nib" "$xcode_dir"/
    rm -rf "$project"
    cp -R "$source_xcode_dir/Vintage Emulator Studio.xcodeproj" "$project"

    pbxproj="$project/project.pbxproj"
    if [ -z "$juce_modules_dir" ]; then
        juce_modules_dir=$(sed -n 's#.*path = \(/.*JUCE/modules\)/juce_core;.*#\1#p' "$source_xcode_dir/Vintage Emulator Studio.xcodeproj/project.pbxproj" | sed -n '1p')
    fi
    test -n "$juce_modules_dir"
    test -d "$juce_modules_dir"
    perl -0pi -e 's#\.\./\.\./validation/mame-0\.289-patched/build/#../../validation/mame-0.289-patched/build-macos-x86_64/#g' "$pbxproj"
    perl -0pi -e 's#VALID_ARCHS = "arm64";#VALID_ARCHS = "x86_64";#g' "$pbxproj"
    perl -0pi -e 's#EXCLUDED_ARCHS = "i386 x86_64 arm64e";#EXCLUDED_ARCHS = "arm64 arm64e i386";#g' "$pbxproj"
    JUCE_MODULES_DIR_VALUE="$juce_modules_dir" perl -0pi -e 's#\Q$ENV{JUCE_MODULES_DIR_VALUE}\E#\$(JUCE_MODULES_DIR)#g' "$pbxproj"
    perl -0pi -e 's#path = (\$\(JUCE_MODULES_DIR\)/[^;]+);#path = "$1";#g' "$pbxproj"
    assert_no_plugin_install_phase "$pbxproj"
    rg -q 'build-macos-x86_64/osx_clang' "$pbxproj"
    ! rg -q '/Users/[^"]*/JUCE/modules' "$pbxproj"
}

build_mame_if_needed() {
    mame_solution="$mame/$mame_builddir/projects/sdl3/mamevesembedded/gmake-osx-clang"

    if [ "$arch" = arm64 ] && [ ! -f "$mame_solution/Makefile" ]; then
        (
            cd "$mame"
            PKG_CONFIG_PATH="$sdl3_prefix/lib/pkgconfig" MACOSX_DEPLOYMENT_TARGET=11.0 \
            make REGENIE=1 SUBTARGET=vesembedded OSD=sdl3 TARGETOS=macosx USE_LIBSDL=1 \
                "$mame_builddir/projects/sdl3/mamevesembedded/gmake-osx-clang/Makefile"
        )
    fi

    if [ "$arch" = x86_64 ] && [ ! -f "$mame_solution/Makefile" ]; then
        (
            cd "$mame"
            PKG_CONFIG_PATH="$sdl3_prefix/lib/pkgconfig" MACOSX_DEPLOYMENT_TARGET=11.0 \
            make -n REGENIE=1 SUBTARGET=vesembedded OSD=sdl3 TARGETOS=macosx PLATFORM=x86 PTR64=1 USE_LIBSDL=1 \
                BUILDDIR="$mame_builddir" \
                ARCHOPTS="-arch x86_64 -mmacosx-version-min=11.0" \
                ARCHOPTS_C="-arch x86_64 -mmacosx-version-min=11.0" \
                ARCHOPTS_CXX="-arch x86_64 -mmacosx-version-min=11.0" \
                ARCHOPTS_OBJC="-arch x86_64 -mmacosx-version-min=11.0" \
                ARCHOPTS_OBJCXX="-arch x86_64 -mmacosx-version-min=11.0" \
                -j2 > "$work/mame-x86_64-dry-run"
        )
        rg -q -- 'build-macos-x86_64' "$work/mame-x86_64-dry-run"
    fi

    if [ "$arch" = arm64 ]; then
        MACOSX_DEPLOYMENT_TARGET=11.0 make -C "$mame_solution" config=release64 -j2 all
    else
        (
            cd "$mame"
            PKG_CONFIG_PATH="$sdl3_prefix/lib/pkgconfig" MACOSX_DEPLOYMENT_TARGET=11.0 \
            make REGENIE=1 SUBTARGET=vesembedded OSD=sdl3 TARGETOS=macosx PLATFORM=x86 PTR64=1 USE_LIBSDL=1 \
                BUILDDIR="$mame_builddir" \
                ARCHOPTS="-arch x86_64 -mmacosx-version-min=11.0" \
                ARCHOPTS_C="-arch x86_64 -mmacosx-version-min=11.0" \
                ARCHOPTS_CXX="-arch x86_64 -mmacosx-version-min=11.0" \
                ARCHOPTS_OBJC="-arch x86_64 -mmacosx-version-min=11.0" \
                ARCHOPTS_OBJCXX="-arch x86_64 -mmacosx-version-min=11.0" \
                -j2
        )
    fi
}

collect_mame_link_inputs() {
    pbxproj=$1
    rg -o "../../validation/mame-0\.289-patched/$mame_builddir/osx_clang/[^\"[:space:];]+\\.(a|o)" \
        "$pbxproj" | sort -u > "$work/mame-link-inputs"
    test "$(wc -l < "$work/mame-link-inputs" | tr -d ' ')" = 71
}

validate_mame_inputs() {
    while IFS= read -r path; do
        test -s "$xcode_dir/$path"
        assert_arch_archive_or_binary "$xcode_dir/$path"
        assert_minos_11_archive_or_binary "$xcode_dir/$path"
    done < "$work/mame-link-inputs"
    test "$(grep -c '\.o$' "$work/mame-link-inputs")" = 43
    test "$(grep -c '\.a$' "$work/mame-link-inputs")" = 28
}

validate_bundles() {
    rg -o 'GAME_EXTERN\([A-Za-z0-9_]+\)' \
        "$mame/src/ves/embeddedinstruments/EmbeddedEmulatorEngine.cpp" | \
        sed -E 's/GAME_EXTERN\(([^)]+)\)/\1/' | sort -u > "$work/expected-machines"
    test "$(wc -l < "$work/expected-machines" | tr -d ' ')" = 45

    for bundle in "$stage/Release/Vintage Emulator Studio.app" "$stage/Release/Vintage Emulator Studio.component" "$stage/Release/Vintage Emulator Studio.vst3"; do
        test -d "$bundle"
        binary="$bundle/Contents/MacOS/Vintage Emulator Studio"
        assert_arch_archive_or_binary "$binary"
        vtool -show-build "$binary" | awk '/minos/ { if ($2 != "11.0") exit 1 }'

        nm -gU "$binary" | sed -nE 's/.*_driver_([A-Za-z0-9_]+)$/\1/p' | sort -u > "$work/symbols"
        while IFS= read -r machine; do
            grep -Fxq "$machine" "$work/symbols"
        done < "$work/expected-machines"

        resources="$bundle/Contents/Resources"
        test "$(find "$resources/artwork" -type f | wc -l | tr -d ' ')" = 37
        test "$(find "$resources/plugins" -type f | wc -l | tr -d ' ')" = 3
        ! find "$resources" -type f \( -iname '*.rom' -o -iname '*.nvram' -o \
            -iname '*.iso' -o -iname '*.dsk' -o -iname '*.img' -o -iname '*.sav' -o \
            -iname '*.state' \) | grep .
        ! otool -L "$binary" | rg 'VintageEmulatorStudioMAME289(_Intel|_Win|_Linux)?/'
        assert_no_homebrew_sdl_runtime "$binary"
    done

    jq empty "$stage/Release/Vintage Emulator Studio.vst3/Contents/Resources/moduleinfo.json"
    ! find "$stage/Release" -name '.DS_Store' -o -name '._*' -o -name '*.psd' | grep .
}

build_sdl3_if_needed
prepare_xcode_project

pbxproj="$project/project.pbxproj"
assert_no_plugin_install_phase "$pbxproj"
! rg -q -- '-L/opt/homebrew/lib|-lSDL3' "$pbxproj" "$root/Project/VintageEmulatorStudio.jucer"
rg -q '\$\(VES_SDL3_PREFIX\)/lib/libSDL3\.a' "$pbxproj"
rg -q '\$\(VES_SDL3_PREFIX\)/lib/libSDL3\.a' "$root/Project/VintageEmulatorStudio.jucer"

build_mame_if_needed
collect_mame_link_inputs "$pbxproj"
validate_mame_inputs

xcodebuild -project "$project" -scheme 'Vintage Emulator Studio - All' \
    -configuration Release -derivedDataPath "$derived_data" build \
    ARCHS="$arch" VALID_ARCHS="$arch" EXCLUDED_ARCHS="$excluded_archs" \
    MACOSX_DEPLOYMENT_TARGET=11.0 CONFIGURATION_BUILD_DIR="$stage/Release" \
    VES_SDL3_PREFIX="$sdl3_prefix" JUCE_MODULES_DIR="$juce_modules_dir"

"$root/platform/macos/normalize-vst3-moduleinfo.sh" \
    "$stage/Release/juce_vst3_helper" \
    "$stage/Release/Vintage Emulator Studio.vst3/Contents/Resources/moduleinfo.json"

validate_bundles

! rg -n '[m]ame0288-source' \
    "$root/Source" \
    "$root/Project" \
    "$pbxproj" \
    "$root/platform/macos/build-macos-release.sh" \
    "$root/platform/macos/MACOS_BUILD.md"
