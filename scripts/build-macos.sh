#!/usr/bin/env bash
# Build from the official OBS plugintemplate, preserving upstream dependency hashes.
# Requires macOS, Xcode 16+ CLT and CMake 3.30+.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="obs-beauty-macos"
WORK="$ROOT/.work/obs-plugintemplate"
DIST="$ROOT/dist"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "[ERROR] Native .plugin builds require macOS with Xcode." >&2
  exit 1
fi
for cmd in git cmake xcodebuild python3; do
  command -v "$cmd" >/dev/null || { echo "Missing dependency: $cmd" >&2; exit 1; }
done

python3 "$ROOT/tests/source_smoke.py"
mkdir -p "$ROOT/.work" "$DIST"
if [[ ! -d "$WORK/.git" ]]; then
  echo "[1/5] Fetching official OBS plugintemplate..."
  git clone --depth 1 https://github.com/obsproject/obs-plugintemplate.git "$WORK"
else
  echo "[1/5] Reusing cached OBS plugintemplate checkout."
fi

echo "[2/5] Applying beauty-filter sources and package metadata..."
cp "$ROOT/src/plugin-main.c" "$WORK/src/plugin-main.c"
rm -rf "$WORK/data"
mkdir -p "$WORK/data"
cp -R "$ROOT/data/." "$WORK/data/"
python3 - "$WORK/buildspec.json" <<'PY'
import json, pathlib, sys
path = pathlib.Path(sys.argv[1]); obj = json.loads(path.read_text())
obj.update({
    'name': 'obs-beauty-macos',
    'displayName': 'OBS Beauty Filter (macOS)',
    'version': '0.1.0',
    'author': 'Ducafecat',
    'website': 'https://ducafecat.com',
    'email': '',
})
obj.setdefault('platformConfig', {}).setdefault('macos', {})['bundleId'] = 'com.ducafecat.obs-beauty-macos'
path.write_text(json.dumps(obj, indent=4, ensure_ascii=False) + '\n')
PY

echo "[3/5] Configuring Xcode Universal (arm64 + x86_64)..."
(
 cd "$WORK"
 cmake --preset macos -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
)
echo "[4/5] Compiling native OBS bundle..."
(
 cd "$WORK"
 cmake --build --preset macos --parallel 4
)

# The official template emits a .plugin bundle into its Xcode build directory.
BUILT="$(find "$WORK/build_macos" -type d -name "$NAME.plugin" \
  -not -path '*/rundir/*' -not -path '*/install/*' | head -n 1)"
if [[ -z "$BUILT" ]]; then
  echo "[ERROR] Build reported success but $NAME.plugin was not found" >&2
  exit 1
fi

echo "[5/5] Packaging for local installation..."
rm -rf "$DIST/$NAME.plugin"
ditto "$BUILT" "$DIST/$NAME.plugin"
# Ad-hoc signing is sufficient for personal testing; not for public distribution.
codesign --force --deep --sign - "$DIST/$NAME.plugin"
# Fix up a clean zip for convenient transport.
rm -f "$DIST/$NAME-macos-universal.zip"
(cd "$DIST" && ditto -c -k --sequesterRsrc --keepParent "$NAME.plugin" "$NAME-macos-universal.zip")

echo
echo "Built: $DIST/$NAME.plugin"
echo "ZIP:   $DIST/$NAME-macos-universal.zip"
echo "To install locally: bash scripts/install-macos.sh"
