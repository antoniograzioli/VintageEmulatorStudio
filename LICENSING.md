# Licensing

Vintage Emulator Studio contains components under multiple licenses.  The root
`LICENSE` identifies the license for original VES source code owned by the
project; it does not automatically relicense MAME, JUCE, SDL, fonts, artwork,
or other third-party content.

## Component Map

| Component | Location | License stated for this repository | Notes |
|---|---|---|---|
| Original VES application/plugin source | `Source/` | AGPL-3.0-only unless a file states another license | Main JUCE processor/editor code and VES host logic. |
| JUCE | External JUCE checkout referenced by project files; generated wrappers under `JuceLibraryCode/` and platform project output | JUCE 8 dual licensing applies: AGPLv3 or the applicable JUCE commercial licence; upstream JUCE terms apply.| JUCE source modules are not vendored as a complete third-party source tree. Existing JUCE-generated notices are preserved. |
| MAME 0.289 | `validation/mame-0.289-patched/` | MAME as a whole is GPL-2.0-or-later per MAME documentation; individual file licenses are retained | Upstream baseline is tag `mame0289`, commit `f34f02505e32c1993c6a782b6814232cbfc74e36`. |
| VES-specific MAME integration files | `validation/mame-0.289-patched/src/ves/embeddedinstruments/`, `validation/mame-0.289-patched/scripts/target/mame/vesembedded.lua` | BSD-3-Clause where marked | These files are intentionally kept under their stated BSD-3-Clause metadata. |
| SDL3 | `third_party/sdl3-src/` | Zlib license text in `third_party/sdl3-src/LICENSE.txt` | Pinned SDL 3.4.14 source, commit `147a8ee32dbf9ac02f3794964490687b6bbda1bc`; macOS builds link static SDL3. |
| Inter font | `Resources/Fonts/` | SIL Open Font License 1.1 | License text is retained in `Resources/Fonts/OFL.txt`. |
| Bundled free artwork/layout resources | `artwork/` | CC0-1.0 where explicitly marked or listed below | Explicit CC0 declarations remain in the layout files. Additional project-owned free artwork files listed below are documented as CC0-1.0. |
| VES logo and branding | `Resources/ves-logo.png`, project name, product name, visual branding | Excluded from the AGPL software license grant and the CC0 artwork dedication | No trademark registration claim is made here. |
| Other third-party files | Vendored/generated locations throughout the repository | Respective upstream licenses/notices | Third-party license and notice files are retained with their source trees or resources. |

## License Texts And Notices

The repository currently includes these local license texts/notices:

- `LICENSE`: AGPL-3.0-only text for original VES source code.
- `LICENSES/AGPL-3.0-only.txt`: AGPL-3.0-only text.
- `LICENSES/GPL-2.0.txt`: GPL-2.0 text retained from MAME legal files.
- `LICENSES/BSD-3-Clause.txt`: BSD-3-Clause text retained from MAME legal files.
- `LICENSES/Zlib.txt`: Zlib text retained from MAME legal files.
- `LICENSES/CC0-1.0.txt`: CC0 text retained from MAME legal files.
- `LICENSES/OFL-1.1.txt`: Inter SIL Open Font License 1.1 notice.
- `validation/mame-0.289-patched/COPYING` and `validation/mame-0.289-patched/docs/legal/`: upstream MAME license data.
- `third_party/sdl3-src/LICENSE.txt`: upstream SDL3 license notice.
- `Resources/Fonts/OFL.txt`: Inter font license notice.

## Project-Owned Free Artwork

The following bundled artwork/layout resources are documented by the VES
project as CC0-1.0 for the free source distribution:

- `artwork/cd3000i/akai.png`
- `artwork/cd3000i/cd3000i.lay`
- `artwork/cd3000i/cd3000i.png`
- `artwork/cd3000xl/akai.png`
- `artwork/cd3000xl/cd3000xl.lay`
- `artwork/cd3000xl/cd3000xl.png`
- `artwork/fb01/default.lay`
- `artwork/fb01/fb01.png`
- `artwork/mu2000/mu2000.lay`
- `artwork/mu2000/mu2000.png`
- `artwork/mu50.zip`
- `artwork/mu50/default.lay`
- `artwork/mu50/mu50.png`
- `artwork/mu50/mu50_curves.svg`
- `artwork/prophet5rev30/default.lay`
- `artwork/s2000/akai.png`
- `artwork/s2000/default.lay`
- `artwork/s3000/akai.png`
- `artwork/s3000/default.lay`
- `artwork/s3000/s3000.png`
- `artwork/s3000xl/akai.png`
- `artwork/s3000xl/s3000xl.lay`
- `artwork/s3000xl/s3000xl.png`
- `artwork/sixtrak/default.lay`
- `artwork/tg100/default.lay`
- `artwork/tg100/tg100.png`
- `artwork/tx81z/default.lay`
- `artwork/tx81z/tx81z.png`

This CC0-1.0 documentation does not apply to the VES logo, VES name, product
name, project branding, ROMs, firmware, NVRAM, user media, or copyrighted
machine media.  Those machine data files are not distributed in this
repository.
