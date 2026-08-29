#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
MAKE_DIR="$ROOT_DIR/Builds/LinuxMakefile"
MAKEFILE="$MAKE_DIR/Makefile"
INTEGRATION_FILE="$MAKE_DIR/LinuxMameIntegration.mk"
JUCER_FILE="$ROOT_DIR/platform/linux/VintageEmulatorStudio.linux.jucer"
MAME_BUILD="$ROOT_DIR/validation/mame-0.289-patched/build/linux_gcc"
DIST_ROOT="$ROOT_DIR/Dist/Linux/x86_64"

integrate_only=0
regenerate=0

usage()
{
    printf 'Usage: %s [--integrate-only] [--regenerate]\n' "${0##*/}"
}

for argument in "$@"; do
    case "$argument" in
        --integrate-only) integrate_only=1 ;;
        --regenerate) regenerate=1 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done

find_projucer()
{
    if [[ -n "${PROJUCER:-}" && -x "$PROJUCER" ]]; then
        printf '%s\n' "$PROJUCER"
        return
    fi

    if command -v Projucer >/dev/null 2>&1; then
        command -v Projucer
        return
    fi

    if [[ -n "${JUCE_ROOT:-}" ]]; then
        local candidate="$JUCE_ROOT/extras/Projucer/Builds/LinuxMakefile/build/Projucer"
        [[ -x "$candidate" ]] && printf '%s\n' "$candidate" && return
    fi

    printf 'Projucer not found. Set PROJUCER or JUCE_ROOT.\n' >&2
    return 1
}

normalize_vst3_moduleinfo()
{
    local helper=$1 manifest=$2 temporary
    command -v python3 >/dev/null 2>&1 || { printf 'python3 is required to validate VST3 moduleinfo.json.\n' >&2; exit 1; }
    temporary="${manifest}.tmp.$$"
    trap 'rm -f "$temporary"' RETURN

    "$helper" > "$temporary"
    perl -0pi -e 's/,[[:space:]]*([}\]])/$1/g' "$temporary"
    python3 -m json.tool "$temporary" >/dev/null
    mv "$temporary" "$manifest"
    trap - RETURN
    python3 -m json.tool "$manifest" >/dev/null
}

apply_configured_juce_root()
{
    if ! grep -F '/media/antonio/Archivio/JUCE' "$MAKEFILE" >/dev/null; then
        return
    fi

    [[ -n "${JUCE_ROOT:-}" ]] || {
        printf 'Generated Makefile contains a nonportable JUCE path. Set JUCE_ROOT or regenerate with a configured Projucer.\n' >&2
        exit 1
    }
    [[ -d "$JUCE_ROOT/modules" ]] || {
        printf 'JUCE_ROOT does not contain a modules directory: %s\n' "$JUCE_ROOT" >&2
        exit 1
    }

    perl -0pi -e 's/\/media\/antonio\/Archivio\/JUCE/\$\(JUCE_ROOT\)/g' "$MAKEFILE"
}

apply_linux_integration()
{
    [[ -f "$MAKEFILE" ]] || { printf 'Missing generated Makefile: %s\n' "$MAKEFILE" >&2; exit 1; }
    [[ -f "$INTEGRATION_FILE" ]] || { printf 'Missing integration file: %s\n' "$INTEGRATION_FILE" >&2; exit 1; }

    apply_configured_juce_root

    local temporary
    temporary=$(mktemp "$MAKE_DIR/.Makefile.integrate.XXXXXX")

    if ! grep -qx 'include LinuxMameIntegration.mk' "$MAKEFILE"; then
        awk '
            BEGIN { inserted = 0 }
            /^OBJECTS_ALL :=/ && ! inserted {
                print "include LinuxMameIntegration.mk"
                print ""
                inserted = 1
            }
            { print }
            END { if (! inserted) exit 42 }
        ' "$MAKEFILE" > "$temporary"
        mv "$temporary" "$MAKEFILE"
    else
        rm -f "$temporary"
    fi

    if ! grep -F '$(JUCE_LDFLAGS_VST3_MANIFEST_HELPER)' "$MAKEFILE" >/dev/null; then
        temporary=$(mktemp "$MAKE_DIR/.Makefile.integrate.XXXXXX")
        awk '
            /\$\(CXX\) -o .*JUCE_TARGET_VST3_MANIFEST_HELPER.*OBJECTS_VST3_MANIFEST_HELPER/ {
                print $0 " $(JUCE_LDFLAGS_VST3_MANIFEST_HELPER)"
                patched = 1
                next
            }
            { print }
            END { if (! patched) exit 43 }
        ' "$MAKEFILE" > "$temporary"
        mv "$temporary" "$MAKEFILE"
    fi

    local include_line objects_line helper_hooks
    include_line=$(grep -n '^include LinuxMameIntegration.mk$' "$MAKEFILE" | cut -d: -f1)
    objects_line=$(grep -n '^OBJECTS_ALL :=' "$MAKEFILE" | cut -d: -f1)
    helper_hooks=$(grep -Fc '$(JUCE_LDFLAGS_VST3_MANIFEST_HELPER)' "$MAKEFILE")
    [[ "$include_line" -lt "$objects_line" && "$helper_hooks" -eq 1 ]] || {
        printf 'Linux integration verification failed after patching Makefile.\n' >&2
        exit 1
    }

    printf 'Linux integration active in %s\n' "$MAKEFILE"
}

if [[ "$regenerate" -eq 1 ]]; then
    projucer=$(find_projucer)
    printf 'Regenerating with %s\n' "$projucer"
    "$projucer" --resave "$JUCER_FILE"
fi

apply_linux_integration
[[ "$integrate_only" -eq 1 ]] && exit 0

required_mame_artifacts=(
    "$MAME_BUILD/obj/x64/Release/src/mame/yamaha/ymtx81z.o"
    "$MAME_BUILD/obj/x64/Release/src/devices/bus/midi/midi.o"
    "$MAME_BUILD/bin/x64/Release/libemu.a"
    "$MAME_BUILD/bin/x64/Release/libosd_sdl.a"
    "$MAME_BUILD/bin/x64/Release/mame_vesembedded/liboptional.a"
    "$MAME_BUILD/bin/x64/Release/mame_vesembedded/libformats.a"
    "$MAME_BUILD/bin/x64/Release/liblua.a"
)

for artifact in "${required_mame_artifacts[@]}"; do
    [[ -s "$artifact" ]] || { printf 'Missing Phase 1 MAME artifact: %s\n' "$artifact" >&2; exit 1; }
done

make_args=(CONFIG=Release CXX=g++ CC=gcc JUCE_VST3DESTDIR=)

printf 'Cleaning JUCE Release outputs only (MAME is not touched).\n'
make -C "$MAKE_DIR" "${make_args[@]}" clean

mapfile -t lua_targets < <(
    awk '
        /^OBJECTS_SHARED_CODE :=/ { shared = 1; next }
        shared && /^OBJECTS_VST3_MANIFEST_HELPER :=/ { exit }
        shared && /\$\(JUCE_OBJDIR\)\/luaengine[^ ]*\.o/ {
            for (i = 1; i <= NF; ++i) {
                if ($i ~ /^\$\(JUCE_OBJDIR\)\/luaengine[^ ]*\.o/) {
                    gsub(/^\$\(JUCE_OBJDIR\)\//, "build/intermediate/Release/", $i)
                    print $i
                }
            }
        }
    ' "$MAKEFILE"
)

[[ "${#lua_targets[@]}" -gt 0 ]] || { printf 'Could not identify Lua frontend targets.\n' >&2; exit 1; }
printf 'Building memory-heavy Lua frontend objects serially.\n'
make -C "$MAKE_DIR" -j1 "${make_args[@]}" "${lua_targets[@]}"

printf 'Building Release Standalone with at most two jobs.\n'
make -C "$MAKE_DIR" -j2 "${make_args[@]}" Standalone
printf 'Building Release VST3 with at most two jobs (installation disabled).\n'
make -C "$MAKE_DIR" -j2 "${make_args[@]}" VST3

standalone_build="$MAKE_DIR/build/Vintage Emulator Studio"
vst3_build="$MAKE_DIR/build/Vintage Emulator Studio.vst3"
vst3_so_build="$vst3_build/Contents/x86_64-linux/Vintage Emulator Studio.so"
[[ -x "$standalone_build" && -f "$vst3_so_build" ]] || { printf 'Expected JUCE outputs are missing.\n' >&2; exit 1; }
normalize_vst3_moduleinfo "$MAKE_DIR/build/juce_vst3_helper" "$vst3_build/Contents/Resources/moduleinfo.json"

printf 'Creating clean Dist tree.\n'
rm -rf "$DIST_ROOT"
mkdir -p "$DIST_ROOT/Standalone/Resources" "$DIST_ROOT/VST3"
cp "$standalone_build" "$DIST_ROOT/Standalone/Vintage Emulator Studio"
cp -R "$MAKE_DIR/build/Resources/plugins" "$DIST_ROOT/Standalone/Resources/"
cp -R "$MAKE_DIR/build/Resources/artwork" "$DIST_ROOT/Standalone/Resources/"
cp -R "$vst3_build" "$DIST_ROOT/VST3/"
find "$DIST_ROOT" -type f \( -name '.DS_Store' -o -name '._*' -o -name '*.psd' -o -name '*~' \) -delete

standalone_dist="$DIST_ROOT/Standalone/Vintage Emulator Studio"
vst3_dist="$DIST_ROOT/VST3/Vintage Emulator Studio.vst3"
vst3_so_dist="$vst3_dist/Contents/x86_64-linux/Vintage Emulator Studio.so"

standalone_before=$(stat -c %s "$standalone_dist")
vst3_before=$(stat -c %s "$vst3_so_dist")
strip --strip-debug "$standalone_dist"
strip --strip-debug "$vst3_so_dist"
standalone_after=$(stat -c %s "$standalone_dist")
vst3_after=$(stat -c %s "$vst3_so_dist")

validate_elf()
{
    local binary=$1 expected_file_pattern=$2
    file "$binary" | grep -E "ELF 64-bit.*${expected_file_pattern}" >/dev/null
    file "$binary" | grep -F 'x86-64' >/dev/null
    readelf -h "$binary" | grep 'Advanced Micro Devices X86-64' >/dev/null
    ! readelf -d "$binary" | grep TEXTREL >/dev/null
    ldd "$binary" | grep 'libstdc++.so' >/dev/null
    ! readelf -d "$binary" | grep -Ei 'portaudio|pipewire|pulse' >/dev/null
}

validate_machines()
{
    local binary=$1 symbols missing=0 expected=0
    symbols=$(mktemp)
    nm -C --defined-only "$binary" > "$symbols"
    while IFS= read -r driver; do
        expected=$((expected + 1))
        grep -Eq "(^|[[:space:]])driver_${driver}$" "$symbols" || missing=$((missing + 1))
    done < <(
        sed -n '/constexpr EmbeddedMachineProfile machineProfiles\[\]/,/^};/p' "$ROOT_DIR/Source/VintageEmulatorStudioProcessor.cpp" \
            | sed -n 's/.*{ "[^"]*", "\([^"]*\)".*/\1/p'
    )
    rm -f "$symbols"
    [[ "$expected" -eq 45 && "$missing" -eq 0 ]]
}

validate_release_resources()
{
    local resources=$1 label=$2 plugin_count artwork_count
    plugin_count=$(find "$resources/plugins" -type f | wc -l)
    artwork_count=$(find "$resources/artwork" -type f | wc -l)
    [[ "$plugin_count" -eq 3 && "$artwork_count" -eq 37 ]] || {
        printf '%s resource count mismatch: %s plugin files, %s artwork files\n' \
            "$label" "$plugin_count" "$artwork_count" >&2
        exit 1
    }
}

validate_elf "$standalone_dist" 'executable'
validate_elf "$vst3_so_dist" 'shared object'
validate_machines "$standalone_dist"
validate_machines "$vst3_so_dist"
[[ -s "$vst3_dist/Contents/Resources/moduleinfo.json" ]]
python3 -m json.tool "$vst3_dist/Contents/Resources/moduleinfo.json" >/dev/null
[[ -s "$DIST_ROOT/Standalone/Resources/plugins/boot.lua" ]]
[[ -s "$DIST_ROOT/Standalone/Resources/plugins/layout/init.lua" ]]
[[ -s "$vst3_dist/Contents/Resources/plugins/boot.lua" ]]
[[ -s "$vst3_dist/Contents/Resources/plugins/layout/init.lua" ]]
validate_release_resources "$DIST_ROOT/Standalone/Resources" "Standalone"
validate_release_resources "$vst3_dist/Contents/Resources" "VST3"

if find "$DIST_ROOT" -type f \( \
    -iname '*.rom' -o -iname '*.chd' -o -iname '*.iso' -o -iname '*.nv' -o \
    -iname '*.nvram' -o -iname '*.dsk' -o -iname '*.hdd' -o -iname '*.hd' -o \
    -iname '*.hdv' -o -iname '*.hdi' -o -iname '*.hds' -o -iname '*.2mg' -o \
    -iname '*.cue' -o -iname '*.nrg' -o -iname '*.gdi' -o -iname '*.cdr' -o \
    -iname '*.ini' -o -iname '*.cfg' -o -iname '*.sta' -o -iname '*.diff' -o \
    -name '.DS_Store' -o -name '._*' -o -name '*.psd' -o -name '*~' \) -print -quit | grep . >/dev/null; then
    printf 'Forbidden runtime/development file found in Dist.\n' >&2
    exit 1
fi

printf 'Standalone bytes: %s unstripped, %s stripped\n' "$standalone_before" "$standalone_after"
printf 'VST3 .so bytes: %s unstripped, %s stripped\n' "$vst3_before" "$vst3_after"
printf 'Standalone resources: %s plugin files, %s artwork files\n' \
    "$(find "$DIST_ROOT/Standalone/Resources/plugins" -type f | wc -l)" \
    "$(find "$DIST_ROOT/Standalone/Resources/artwork" -type f | wc -l)"
printf 'VST3 resources: %s plugin files, %s artwork files\n' \
    "$(find "$vst3_dist/Contents/Resources/plugins" -type f | wc -l)" \
    "$(find "$vst3_dist/Contents/Resources/artwork" -type f | wc -l)"
printf 'Linux release staged and statically validated at %s\n' "$DIST_ROOT"
