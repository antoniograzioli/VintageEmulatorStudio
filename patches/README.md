# VES MAME patch sets

Each directory is a Git-compatible, ordered patch series against the exact official MAME tag named by the directory.  Patches exclude ROMs, NVRAM, build output, generated translations and local paths.  Apply with `scripts/apply-ves-mame-patches.sh <tree> patches/mame-0.288` and validate with `scripts/check-ves-mame-patches.sh <tree>`.

Maintain patches by regenerating them from a clean official tree after every port; do not hand-edit a patched upstream checkout without regenerating its series.
