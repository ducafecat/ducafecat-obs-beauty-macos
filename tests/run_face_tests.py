#!/usr/bin/env python3
"""Production geometry tests; optional macOS Vision and installed OBS integration."""
import argparse
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--native", action="store_true", help="also run real Apple Vision lifecycle tests")
parser.add_argument("--obs-bundle", type=Path, help="also test a built .plugin with /Applications/OBS.app")
args = parser.parse_args()


def run(command):
    subprocess.run(command, cwd=root, check=True, timeout=120)


with tempfile.TemporaryDirectory(prefix="beauty-face-tests-") as directory:
    temporary = Path(directory)
    binary = str(temporary / "face-tests")
    run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Isrc",
         "tests/face_behavior.c", "src/face-mask.c", "-lm", "-o", binary])
    run([binary])
    if args.native:
        obj = str(temporary / "face-mask.o")
        binary = str(temporary / "vision-tests")
        run(["clang", "-std=c11", "-c", "src/face-mask.c", "-o", obj])
        run(["clang++", "-std=c++17", "-fobjc-arc", "-Isrc", "tests/vision_lifecycle.mm",
             "src/face-tracker.mm", obj, "-framework", "Vision", "-framework", "Foundation",
             "-framework", "CoreVideo", "-framework", "CoreGraphics", "-o", binary])
        run([binary])
    if args.obs_bundle:
        frameworks = "/Applications/OBS.app/Contents/Frameworks"
        headers = root / ".work/obs-plugintemplate/.deps/Frameworks/libobs.framework/Headers"
        if not headers.is_dir():
            parser.error("Build the plugin first to obtain OBS SDK headers.")
        bundle = args.obs_bundle.resolve()
        binary = str(temporary / "obs-tests")
        run(["clang++", "-std=c++17", "-F", frameworks, "-I", str(headers),
             "tests/obs_runtime_smoke.mm", "-framework", "libobs", "-framework", "AppKit",
             f"-Wl,-rpath,{frameworks}", "-o", binary])
        run([binary, str(bundle / "Contents/MacOS/obs-beauty-macos"), str(bundle / "Contents/Resources")])
