#!/usr/bin/env bash
# Builds the firmware and publishes it as a GitHub Release, so clocks can update
# themselves from the app ("Atualizar firmware").
#
# Usage:  tools/release.sh
# Needs:  PlatformIO, and the GitHub CLI (gh) logged in as the repo owner.
#         Without gh, it prints the steps to publish through the website.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION=$(grep -oP '#define FW_VERSION "\K[0-9.]+' firmware/src/main.cpp)
TAG="v$VERSION"
BIN=firmware/.pio/build/supermini/firmware.bin
echo "Firmware version: $VERSION"

if git rev-parse "$TAG" >/dev/null 2>&1; then
  echo "Tag $TAG already exists. Bump FW_VERSION in firmware/src/main.cpp first." >&2
  exit 1
fi

(cd firmware && pio run)
ls -l "$BIN"

if command -v gh >/dev/null && gh auth status >/dev/null 2>&1; then
  git tag "$TAG"
  git push origin "$TAG"
  gh release create "$TAG" "$BIN#firmware.bin" --repo paulohsm/esp32c3-clock \
    --title "$TAG" --notes "Firmware $VERSION"
  echo "Published $TAG. The app will offer it within 6 h, or at once with 'Verificar atualização'."
else
  cat <<MSG

gh is not available or not logged in. Publish through the website:
  1. Open https://github.com/paulohsm/esp32c3-clock/releases/new
  2. Tag: $TAG (create new tag), title: $TAG
  3. Attach: $(pwd)/$BIN   (the file must be named firmware.bin)
  4. Click "Publish release".
MSG
fi
