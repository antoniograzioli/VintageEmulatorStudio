#!/bin/sh
set -eu

usage() { echo "usage: $0 [--check] <mame-source-dir> <patch-series-dir>" >&2; exit 2; }
check_only=false
[ "${1:-}" = "--check" ] && { check_only=true; shift; }
[ "$#" = 2 ] || usage
target=$1
series_dir=$2
[ -f "$series_dir/series" ] || { echo "missing series: $series_dir/series" >&2; exit 1; }
[ -d "$target/src" ] || { echo "not a MAME source tree: $target" >&2; exit 1; }
target=$(cd "$target" && pwd -P)
series_dir=$(cd "$series_dir" && pwd -P)

# Version-specific VES series must only be applied to their corresponding
# official release checkout.  This is deliberately based on the release tag,
# not a local directory name, so a copied validation tree is accepted too.
case "$(basename "$series_dir")" in
    mame-0.289)
        tag=$(git -C "$target" describe --tags --exact-match 2>/dev/null || true)
        [ "$tag" = "mame0289" ] || {
            echo "mame-0.289 patches require official tag mame0289 (found: ${tag:-unrecognised})" >&2
            exit 1
        }
        ;;
    mame-0.288)
        tag=$(git -C "$target" describe --tags --exact-match 2>/dev/null || true)
        [ "$tag" = "mame0288" ] || {
            echo "mame-0.288 patches require official tag mame0288 (found: ${tag:-unrecognised})" >&2
            exit 1
        }
        ;;
esac

while IFS= read -r patch; do
    [ -n "$patch" ] || continue
    case "$patch" in \#*) continue;; esac
    file="$series_dir/$patch"
    [ -f "$file" ] || { echo "missing patch: $file" >&2; exit 1; }
    if $check_only; then
        echo "checking $patch"
    else
        echo "applying $patch"
    fi
    if $check_only; then git -C "$target" apply --check "$file"; else git -C "$target" apply "$file"; fi
done < "$series_dir/series"
