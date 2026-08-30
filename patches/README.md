# VES MAME patch sets

`patches/mame-0.289/` is a Git-compatible, ordered patch series against official MAME tag `mame0289`.  Patches exclude ROMs, NVRAM, build output, generated translations, and local paths.

Apply and validate the series with:

```sh
scripts/apply-ves-mame-patches.sh /path/to/mame0289 patches/mame-0.289
scripts/check-ves-mame-patches.sh /path/to/mame0289
```

Maintain patches by regenerating them from a clean official MAME 0.289 tree after every VES MAME integration change.
