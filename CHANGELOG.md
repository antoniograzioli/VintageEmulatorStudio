# Vintage Emulator Studio Changelog

## VES 0.9.289.1

### NEW
- Added MIDI SysEx support.
- VES now forwards all incoming MIDI messages transparently to the emulated machine, including aftertouch, MIDI clock and other system/realtime messages.
- Added GUI Performance modes:
  - Normal
  - Reduced
  - Static
  - Disabled
- Added a Volume control to the bottom control bar.
- Added the current VES version number to the interface.
- Added a ROM Rescan button to the ROM Folder window.
- ROM Rescan can automatically retry the currently selected machine if its ROM becomes available.
- Added detailed MAME-derived startup diagnostics for incomplete, incorrect or missing ROM sets and dependencies.

### FIXED
- Fixed SysEx and other valid MIDI messages being filtered out before reaching MAME.
- Fixed the TX81Z front panel artwork and restored proper interactive controls.
- Fixed stale artwork remaining visible after a failed machine load.
- Windows and Linux Standalone builds are now fully self-contained and no longer require an external Resources folder.
- Various CPU and performance optimizations, especially in the audio bridge and GUI refresh path.
- General stability improvements.

### KNOWN LIMITATIONS
- Offline / faster-than-realtime bounce is not currently supported reliably. Use realtime bounce/export when rendering VES tracks.


## VES 0.9.289

First public Beta release
