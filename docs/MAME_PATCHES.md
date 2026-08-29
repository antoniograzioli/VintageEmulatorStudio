# MAME Patches

The production plugin uses the sibling `mame0288-source` tree. These are the
functional MAME-core changes required by the embedded capture path.

## `src/emu/render.h`

### `RENDER_CREATE_CAPTURE`

Adds the generic `RENDER_CREATE_CAPTURE` render-target flag. It identifies a
hidden off-screen target whose referenced screen containers must remain live.
This is generic to embedded capture, not TX81Z-specific. Verify that the flag
value remains unused by upstream MAME before applying it to a newer source.

## `src/emu/render.cpp`

### `render_manager::is_live(screen_device &)`

Treats targets with `RENDER_CREATE_CAPTURE` as live even when hidden. This is
required for screen devices such as the TX81Z HD44780 LCD to populate their
render containers for the software rasterizer. The patch is generic.

### `render_manager::config_save(...)`

Returns safely when there is no parent XML node or no UI target. The embedded
headless OSD intentionally has no native UI target, so upstream shutdown
configuration saving must tolerate that state. This is generic headless-OSD
safety and may be avoidable if a future MAME version makes the UI target
optional.

## Reapply / verify

After an MAME update, verify the three items above before rebuilding. Confirm
that hidden capture targets keep referenced screen containers live and that
shutdown with no UI target does not dereference a null target. No TX81Z-specific
changes are required in these core patches.
