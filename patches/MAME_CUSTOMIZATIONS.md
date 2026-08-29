# VES MAME 0.288 customizations inventory

Comparison base: official MAME `mame0288` (`27a8d9e85b58058965907d1d8a7a92f8ed039348`).  The working comparison tree excludes build output, ROMs, generated translations, Genie binaries and local test data.

| Patch | Classification | Files | Purpose/risk |
|---|---|---|---|
| 0001 | B | `scripts/genie.lua`, `scripts/src/3rdparty.lua`, `scripts/target/mame/vesembedded.lua` | Defines the standalone `vesembedded` MAME target, retains MAME's normal `optional` device archive link, and includes Lua/lualibs/SQLite build products. Required by AU and Standalone; moderate build-system merge risk. |
| 0002 | A | `src/ves/embeddedinstruments/{main,EmbeddedEmulatorEngine}.{cpp,h}` | Embedded manager/OSD, threaded MAME runtime, ring-buffer audio sink and memory MIDI provider. Required by AU and Standalone; VES-specific, not an upstream candidate as-is. |
| 0003 | C | `src/devices/imagedev/midiin.cpp`, `src/emu/{render,screen}.{cpp,h}`, `src/frontend/mame/{luaengine,luaengine_render}.{cpp,h}` | `VES_LAYOUT_PLUGIN_SUPPORT` layout callback and Lua/render access. Required for artwork/layout interaction; high API merge risk. |
| 0010 | D | `src/mame/sequential/prophet5.cpp` | Virtual MIDI keyboard retrofit, guarded by `VES_VIRTUAL_MIDI_RETROFIT`. |
| 0011 | D | `src/mame/linn/linndrum.cpp` | Virtual MIDI drum retrofit, guarded by `VES_VIRTUAL_MIDI_RETROFIT`. |
| 0012 | D | `src/mame/oberheim/dmx.cpp` | Virtual MIDI drum retrofit, guarded by `VES_VIRTUAL_MIDI_RETROFIT`. |
| 0013 | D | `src/mame/yamaha/ympsr11.cpp` | Virtual MIDI keyboard retrofit, guarded by `VES_VIRTUAL_MIDI_RETROFIT`. |
| 0014 | D | `src/mame/yamaha/ympsr150.cpp` | Virtual MIDI PSR/PSS family retrofit, guarded by `VES_VIRTUAL_MIDI_RETROFIT`. |

`3rdparty/lua-zlib/lua_zlib.c` contains only a release-string change and is intentionally classified G (diagnostic/obsolete), not patched.  Added ROM archives, `.mo` translations, build products, and the `mamevesembedded` helper are H (generated/local) and excluded.

The runtime’s active providers are explicitly the embedded OSD audio sink `ves` and memory MIDI port `ves`; it launches MAME with `-sound none`, `-midiprovider none`, `-midiin ves`, and `-midiout ves`. PortAudio/PortMidi may be linked to satisfy the target but must not become runtime hardware providers.

The curated 0.289 target registers 43 instruments. Yamaha TG100 remains selected. Experimental Akai S2000, S3000, S3000XL, CD3000XL and CD3000i support reuse the upstream `akai/s3000.cpp` translation unit, its NEC/floppy/SCSI/DSP/LCD dependency closure, and a generic startup-media option path. VES exposes their shared floppy device as `-flop <path>` only after selecting the requested machine, so image options are registered for the correct driver; malformed startup options become a normal MAME error rather than terminating the AU. No SCSI media UI or upstream driver patch is included. The unexposed Yamaha QS300/EOS translation unit remains absent from the VES target and plug-in linker inputs.
