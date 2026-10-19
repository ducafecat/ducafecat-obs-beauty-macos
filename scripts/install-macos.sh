#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SOURCE="$ROOT/dist/obs-beauty-macos.plugin"
TARGET="$HOME/Library/Application Support/obs-studio/plugins"
if [[ "$(uname -s)" != "Darwin" ]]; then echo "macOS required" >&2; exit 1; fi
if [[ ! -d "$SOURCE" ]]; then
 echo "No compiled bundle at $SOURCE. Run bash scripts/build-macos.sh first." >&2
 exit 1
fi
mkdir -p "$TARGET"
rm -rf "$TARGET/obs-beauty-macos.plugin"
ditto "$SOURCE" "$TARGET/obs-beauty-macos.plugin"
echo "Installed to: $TARGET/obs-beauty-macos.plugin"
echo "Fully quit and reopen OBS, then choose camera source -> Filters -> + -> Beauty Filter (GPU)."
