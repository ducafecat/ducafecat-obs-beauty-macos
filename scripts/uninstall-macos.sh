#!/usr/bin/env bash
set -euo pipefail
TARGET="$HOME/Library/Application Support/obs-studio/plugins/obs-beauty-macos.plugin"
if [[ "$(uname -s)" != "Darwin" ]]; then echo "macOS required" >&2; exit 1; fi
if [[ -d "$TARGET" ]]; then
 rm -rf "$TARGET"
 echo "Removed: $TARGET"
else
 echo "Not installed: $TARGET"
fi
