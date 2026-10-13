#!/usr/bin/env bash
# Prints the notes of one version from CHANGELOG.md (the lines under its
# "## vX.Y.Z" heading, up to the next "## "). Exits with 1 when there are none.
#
# Usage:  tools/notes.sh 1.5.0
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION=${1:?usage: tools/notes.sh X.Y.Z}
NOTES=$(awk -v v="## v$VERSION" '
  index($0, "## ") == 1 { if (on) exit; on = ($0 == v || index($0, v " ") == 1); next }
  on' CHANGELOG.md | sed -e '/./,$!d' | sed -e ':a' -e '/^\n*$/{$d;N;ba' -e '}')

if [ -z "$NOTES" ]; then
  echo "CHANGELOG.md has no notes for v$VERSION." >&2
  exit 1
fi
printf '%s\n' "$NOTES"
