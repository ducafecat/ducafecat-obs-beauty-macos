#!/usr/bin/env python3
"""Lightweight source package sanity checks; does NOT compile the OBS SDK."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
c = (root / "src/plugin-main.c").read_text()
shader = (root / "data/beauty.effect").read_text()
keys = ["smooth_amount", "blur_radius", "whiten_amount", "rosy_amount", "sharpen_amount", "skin_only", "texel_size", "face_mask", "face_only", "feature_protection", "face_freshness", "mask_debug"]
for name in keys:
    assert re.search(rf"\b{name}\b", shader), name
    assert f'"{name}"' in c, name
for f in ["data/locale/zh-CN.ini", "data/locale/en-US.ini", "scripts/build-macos.sh", "scripts/face-tracking.cmake", "src/face-tracker.h", "src/face-tracker.mm", "src/face-mask.c", "LICENSE"]:
    assert (root / f).exists(), f
for x in (c, shader):
    assert x.count("{") == x.count("}"), "unbalanced braces"
assert c.count("gs_effect_set_float") >= 6
assert shader.count("bilateralTap(") >= 13
assert "OBS_SOURCE_TYPE_FILTER" in c
assert "OBS_SOURCE_SRGB" in c
print("Source package smoke checks passed (not a macOS compile or runtime test).")
