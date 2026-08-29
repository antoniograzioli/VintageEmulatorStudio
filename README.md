# Vintage Emulator Studio

Vintage Emulator Studio is an Audio Unit instrument collection for **Synthesizers • Drum Machines • Sound Modules • Samplers**.

The project includes project-owned hosting, artwork, layouts, MIDI retrofit, and audio integration code.  It uses MAME-derived emulation code where required by the applicable upstream licensing and attribution terms.  MAME is not part of the product name and this project does not claim endorsement by the upstream MAME project.

## Local test material

ROMs, firmware, NVRAM, installed plug-ins, and build products are intentionally excluded from source control. Configure legally obtained ROM material through the plug-in's ROM location setting when testing locally. Do not add ROMs, firmware, or personal machine state to a release repository.

## Development

Open `VintageEmulatorStudio.jucer` with Projucer, or build the `Vintage Emulator Studio - AU` target from `Builds/MacOSX/VintageEmulatorStudio.xcodeproj`. The AU identity is `aumu/VES1/AtdF` with bundle identifier `net.autodafe.VintageEmulatorStudio`.
