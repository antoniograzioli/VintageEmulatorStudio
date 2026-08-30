#!/bin/sh
set -eu

helper=$1
manifest=$2
temporary="${manifest}.tmp.$$"
trap 'rm -f "$temporary"' EXIT HUP INT TERM

"$helper" > "$temporary"

# JUCE's current helper emits trailing commas before closing JSON arrays/objects.
# Restrict normalization to that invalid JSON construct.
perl -0pi -e 's/,[[:space:]]*([}\]])/$1/g' "$temporary"
jq empty "$temporary"
mv "$temporary" "$manifest"
trap - EXIT HUP INT TERM
jq empty "$manifest"
