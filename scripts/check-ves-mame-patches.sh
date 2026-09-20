#!/bin/sh
set -eu
[ "$#" -ge 1 ] && [ "$#" -le 2 ] || { echo "usage: $0 <mame-source-dir> [validated-reference-tree]" >&2; exit 2; }
root=$1
for file in scripts/target/mame/vesembedded.lua src/ves/embeddedinstruments/EmbeddedEmulatorEngine.cpp src/ves/embeddedinstruments/main.cpp; do
    [ -f "$root/$file" ] || { echo "missing VES file: $file" >&2; exit 1; }
done
for token in VES_EMBEDDED_BUILD VES_VIRTUAL_MIDI_RETROFIT VES_LAYOUT_PLUGIN_SUPPORT prophet5r30 linndrum obdmx psr11 s2000 s3000 s3000xl cd3000xl cd3000i; do
    grep -R -q "$token" "$root/src" "$root/scripts" || { echo "missing VES marker: $token" >&2; exit 1; }
done
for removed in eosb900 ymqs300; do
    ! grep -q "$removed" "$root/scripts/target/mame/vesembedded.lua" "$root/src/ves/embeddedinstruments/EmbeddedEmulatorEngine.cpp" || {
        echo "removed VES driver still selected: $removed" >&2
        exit 1
    }
done
grep -q 'GAME_EXTERN(tg100);' "$root/src/ves/embeddedinstruments/EmbeddedEmulatorEngine.cpp" || {
    echo "missing retained VES driver: tg100" >&2
    exit 1
}
grep -q 'GAME_EXTERN(s3000xl);' "$root/src/ves/embeddedinstruments/EmbeddedEmulatorEngine.cpp" || {
    echo "missing experimental VES driver: s3000xl" >&2
    exit 1
}
grep -q 'GAME_EXTERN(cd3000xl);' "$root/src/ves/embeddedinstruments/EmbeddedEmulatorEngine.cpp" || {
    echo "missing experimental VES driver: cd3000xl" >&2
    exit 1
}
grep -q 'GAME_EXTERN(cd3000i);' "$root/src/ves/embeddedinstruments/EmbeddedEmulatorEngine.cpp" || {
    echo "missing experimental VES driver: cd3000i" >&2
    exit 1
}
for driver in s2000 s3000; do
    grep -q "GAME_EXTERN($driver);" "$root/src/ves/embeddedinstruments/EmbeddedEmulatorEngine.cpp" || {
        echo "missing experimental VES driver: $driver" >&2
        exit 1
    }
done

if [ "$#" = 2 ]; then
    reference=$2
    files='scripts/genie.lua
scripts/src/3rdparty.lua
scripts/target/mame/vesembedded.lua
src/devices/cpu/drcbearm64.cpp
src/devices/cpu/drcbeut.h
src/devices/cpu/drcbex64.cpp
src/devices/imagedev/midiin.cpp
src/devices/imagedev/midiin.h
src/emu/machine.cpp
src/emu/render.cpp
src/emu/render.h
src/emu/rendersw.hxx
src/emu/rendlay.h
src/emu/sound.cpp
src/emu/sound.h
src/mame/casio/cz101.cpp
src/osd/osdepend.h
src/ves/embeddedinstruments/EmbeddedEmulatorEngine.cpp
src/ves/embeddedinstruments/EmbeddedEmulatorEngine.h
src/ves/embeddedinstruments/main.cpp'
    printf '%s\n' "$files" | while IFS= read -r file; do
        cmp -s "$root/$file" "$reference/$file" || {
            echo "VES source mismatch: $file" >&2
            exit 1
        }
    done
    echo "VES MAME source equivalence passed: $root"
fi
echo "VES MAME patch checks passed: $root"
