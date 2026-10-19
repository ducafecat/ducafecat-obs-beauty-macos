#!/usr/bin/env bash
# Build from the official OBS plugintemplate, preserving upstream dependency hashes.
# Requires macOS, Xcode 16+ CLT and CMake 3.30+.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="obs-beauty-macos"
WORK="$ROOT/.work/obs-plugintemplate"
DIST="$ROOT/dist"
BUILD_DIR="${BEAUTY_BUILD_DIR:-$WORK/build_macos}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "[ERROR] Native .plugin builds require macOS with Xcode." >&2
  exit 1
fi
for cmd in git cmake xcodebuild python3; do
  command -v "$cmd" >/dev/null || { echo "Missing dependency: $cmd" >&2; exit 1; }
done

python3 "$ROOT/tests/source_smoke.py"
python3 "$ROOT/tests/run_face_tests.py"
mkdir -p "$ROOT/.work" "$DIST"
if [[ ! -d "$WORK/.git" ]]; then
  echo "[1/5] Fetching official OBS plugintemplate..."
  git clone --depth 1 https://github.com/obsproject/obs-plugintemplate.git "$WORK"
else
  echo "[1/5] Reusing cached OBS plugintemplate checkout."
fi

echo "[2/5] Applying beauty-filter sources and package metadata..."
cp "$ROOT"/src/*.c "$ROOT"/src/*.h "$ROOT"/src/*.mm "$WORK/src/"
cp "$ROOT/scripts/face-tracking.cmake" "$WORK/face-tracking.cmake"
python3 - "$WORK/CMakeLists.txt" <<'CMAKEPY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
text = path.read_text()
line = 'include("${CMAKE_CURRENT_SOURCE_DIR}/face-tracking.cmake")'
if line not in text:
    path.write_text(text + '\n' + line + '\n')
CMAKEPY
rm -rf "$WORK/data"
mkdir -p "$WORK/data"
cp -R "$ROOT/data/." "$WORK/data/"
python3 - "$WORK/buildspec.json" <<'PY'
import json, pathlib, sys
path = pathlib.Path(sys.argv[1]); obj = json.loads(path.read_text())
obj.update({
    'name': 'obs-beauty-macos',
    'displayName': 'OBS Beauty Filter (macOS)',
    'version': '0.2.0',
    'author': 'Ducafecat',
    'website': 'https://ducafecat.com',
    'email': '',
})
obj.setdefault('platformConfig', {}).setdefault('macos', {})['bundleId'] = 'com.ducafecat.obs-beauty-macos'
path.write_text(json.dumps(obj, indent=4, ensure_ascii=False) + '\n')
PY

# A downloaded/moved checkout may contain an absolute-path CMake cache.
# Preserve it and the dependency cache; use a fresh plugin build directory.
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]] && ! python3 - "$BUILD_DIR/CMakeCache.txt" "$WORK" <<'CACHEPY'
from pathlib import Path
import sys
lines = Path(sys.argv[1]).read_text().splitlines()
home = next((line.split('=', 1)[1] for line in lines if line.startswith('CMAKE_HOME_DIRECTORY:')), '')
sys.exit(0 if home == sys.argv[2] else 1)
CACHEPY
then
  BUILD_DIR="$WORK/build_macos-local"
  echo "[INFO] Preserving relocated cache; building in $BUILD_DIR"
fi

echo "[3/5] Configuring Xcode Universal (arm64 + x86_64)..."
(
 cd "$WORK"
 cmake --preset macos -B "$BUILD_DIR" -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO -DPLUGIN_BUILD_NUMBER=1 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
)
echo "[4/5] Compiling native OBS bundle..."
(
 cd "$WORK"
 cmake --build "$BUILD_DIR" --config RelWithDebInfo --parallel 4
)

# Select the exact configuration just built, never an older cached bundle.
BUILT="$BUILD_DIR/RelWithDebInfo/$NAME.plugin"
if [[ ! -d "$BUILT" ]]; then
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
