# Vintage Emulator Studio Changelog

## VES 0.9.289.2

### NEW
- Generalized machine state save/restore support for compatible instruments. Most machines now remeber their settings if you reopen them (AKAIs still don't as they would need floppy, cds, hdds...)
- Automatic per-machine state recall in Standalone mode.
- Added support for restoring held MIDI note state more safely after loading a machine state.
- New performance optimizations for audio and video processing. Mainly reducing redraws and cahing UI static elements that never change
- New Background Color setting
Image
- MIDI Panic Button


### FIXED
- Lowered startup times. Some machines are now initilized almost instantly.
- Significantly improved audio latency and realtime stability, especially at smaller buffer sizes.
- Fixed MIDI issues after state restore, including stuck notes and unresponsive MIDI input.
- Fixed machine restarts caused by host buffer-size changes.
- Improved video rendering efficiency and reduced unnecessary UI/render workload.
- Fixed several Windows and Linux build/integration issues.
- Various stability improvements



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
