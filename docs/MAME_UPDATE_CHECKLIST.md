# MAME Update Checklist

1. Obtain the new MAME source tree.
2. Apply or verify the patches listed in `docs/MAME_PATCHES.md`.
3. Regenerate the embedded MAME target with TX81Z and AP-10 included.
4. Build the embedded target and required libraries.
5. Test Yamaha TX81Z boot, audio, MIDI, LCD, panel rendering, and clicks.
6. Test Casio AP-10 boot, audio, MIDI, panel rendering, and clicks.
7. Test switching TX81Z to AP-10 and AP-10 to TX81Z.
8. Verify audio latency, MIDI delivery, video capture, crop and click mapping.
9. Verify TX81Z seeded NVRAM and ROM validation.
10. Verify shutdown and editor close/reopen without restarting the machine.
11. Compare audio-ring and video diagnostics against the production baseline.
