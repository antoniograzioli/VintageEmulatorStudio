# MAME 0.289 VES porting report

Upstream baseline: official `mame0289`, commit
`f34f02505e32c1993c6a782b6814232cbfc74e36`.

## Patch results

| Patch | 0.289 result | Notes |
| --- | --- | --- |
| 0001 embedded target/build | clean | Retains the minimal standalone target and clean-build Lua/lualibs/SQLite graph. |
| 0002 runtime/frontend | clean | Embedded engine, in-memory audio/MIDI bridge and lexical driver registry applied unchanged. |
| 0003 layout/Lua | adapted | See below. |
| 0010 Prophet-5 | adapted | Upstream moved `machine_start` context; VES save-state/notifier registrations were reinserted without replacing 0.289 code. |
| 0011 LinnDrum | clean with offsets | Pulse retrofit applied unchanged. |
| 0012 Oberheim DMX | clean with offsets | Pulse retrofit applied unchanged. |
| 0013 Yamaha PSR-11 | adapted | 0.289 has no matching reset body; retained VES declaration and added a minimal reset implementation plus state/notifier setup. |
| 0014 Yamaha GEW7 PSR/PSS | clean with offsets | Shared retrofit applied unchanged. |

## Layout/Lua adaptation

MAME 0.289 adds render-target `ui_event_sink` lifecycle/event forwarding and
changes output binding to `output_proxy`.  Those changes are preserved.  The
VES capture-target liveness and `screen_device::update_quads` cache changes
were reapplied around the new code.  The former `cstdlib` include hunks in
`render.cpp` and `screen.cpp` were obsolete and intentionally omitted.

`luaengine.cpp` no longer has the old output-manager binding context used by
0.288.  The port keeps the new output-proxy API and installs the guarded,
lightweight VES `machine_manager` binding immediately before the normal
desktop-manager branch.  The compact ioport/layout bindings and render
bindings remain under `VES_LAYOUT_PLUGIN_SUPPORT`; desktop UI/input/debug
bindings remain excluded for the embedded target.

This restores layout plugin initialization, callback lookup and continuous
ioport control registration without enabling the desktop MAME UI stack.

## Obsolete/upstream changes

Only the two include-only hunks above and the historical `lua_zlib` diagnostic
version string are obsolete.  No VES runtime or retrofit behavior is treated
as upstreamed or silently dropped.

## Reproduction

`patches/mame-0.289/series` was dry-run and applied in order to a fresh clone
at `validation/mame-0.289-patched`; each patch applies with no manual edit.
`scripts/apply-ves-mame-patches.sh` now validates `mame0289` for this series.

## Curated driver scope

The embedded registry contains 43 instruments. Yamaha TG100 remains registered. Experimental Akai S2000, S3000, S3000XL, CD3000XL and CD3000i are selected from their shared upstream `akai/s3000.cpp` driver and required NEC, uPD765, NSCSI, MB87030, L7A1045 and HD61830 closure. VES passes a selected floppy image as separate `-flop` and path arguments at startup; changing/clearing media restarts a floppy-capable embedded engine. The Yamaha QS300/EOS source is part of upstream MAME 0.289 but is intentionally neither selected by the VES target nor linked by the VES plug-in.

## Standalone optional-library generation correction

The VES target sets `STANDALONE`, which correctly suppresses the normal driver
and frontend projects. `devicesProject` declares the populated `optional`
project for the selected VES CPU, machine, sound, video, bus and format
sources. Upstream `main.lua` already uses the valid MAME/Genie relationship:
`links { "optional", "emu" }`; its generated make target must therefore list
`liboptional.a` as a prerequisite of the final link. An attempted
`dependson { "optional" }` addition was removed because `dependson` is not a
Genie scripting API. The clean-build validation now tests the existing normal
link relationship rather than replacing it with a custom dependency mechanism.

### Clean-build follow-up: VA VCO selector

The first correctly ordered final link exposed one missing selected-device
source: `SOUNDS["VA_VCO"]`.  It supplies `src/devices/sound/vavco.cpp` and the
`va_vco_device` implementation used by Prophet-5 and the selected CEM3340 and
CEM3394 devices.  The selector has been added to `vesembedded.lua`; it is a
dependency-closure correction, not a driver or feature removal.

## Clean embedded-target validation

On 2026-08-04, a fresh tree cloned from official `mame0289`
(`f34f02505e32c1993c6a782b6814232cbfc74e36`) accepted the complete
`patches/mame-0.289/series` without a manual source edit. The patch check
passed, followed by a clean successful `make SUBTARGET=vesembedded -j4`.
The resulting executable is `mamevesembedded`; the populated selected-device
archive is `build/osx_clang/bin/x64/Release/mame_vesembedded/liboptional.a`.
The generated main target lists `liboptional.a` through `LIBDEPS`, so it is a
prerequisite of the final link under parallel make.

## Follow-up embedded runtime API port

The first fresh build exposed the MAME 0.289 move of pointer event injection
from `ui_input_manager` to `render_target`. `EmbeddedEmulatorEngine` now calls
`m_video_target->push_pointer_update` for queued mouse events and the synthetic
release. Event type, target, coordinates and button transitions are unchanged;
this preserves the layout-plugin pointer path without restoring an obsolete UI
input API.
