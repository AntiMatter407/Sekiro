"""
Extract a small subset of common Sekiro animations from Sekiro_animations.json.

Usage:
  python3 extract_common_anims.py
"""

import json

SRC = r"D:\Sekiro\Extracted\Sekiro_animations.json"
DST = r"D:\Sekiro\Extracted\Sekiro_common_anims.json"

WANTED = {
    "Sekiro_a000_000000": "Sekiro_Idle",
    "Sekiro_a000_000100": "Sekiro_Walk",
    "Sekiro_a000_000200": "Sekiro_Run",
    "Sekiro_a000_000400": "Sekiro_Sprint",
}

print(f"Loading {SRC}")
with open(SRC, "r", encoding="utf-8") as f:
    data = json.load(f)

animations = []
for anim in data["Animations"]:
    name = anim["Name"]
    if name in WANTED:
        anim = dict(anim)
        anim["OriginalName"] = name
        anim["Name"] = WANTED[name]
        animations.append(anim)
        print(f"Found {name} -> {anim['Name']} ({anim['FrameCount']} frames)")

missing = [k for k in WANTED if not any(a.get("OriginalName") == k for a in animations)]
if missing:
    print("Missing:", missing)

out = {
    "BoneCount": data["BoneCount"],
    "BoneNames": data["BoneNames"],
    "BoneParents": data["BoneParents"],
    "BoneLocalTransforms": data.get("BoneLocalTransforms", []),
    "AnimationCount": len(animations),
    "Animations": animations,
}

with open(DST, "w", encoding="utf-8") as f:
    json.dump(out, f, separators=(",", ":"))

print(f"Wrote {DST}")
