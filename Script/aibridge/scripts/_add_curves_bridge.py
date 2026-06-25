import sys
import os
import json
import glob

import subprocess

BRIDGE_PY = r"D:\Sekiro\Script\aibridge\bridge.py"
UE_PY = r"D:\Program Files\Epic Games\UE_5.2\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
ANIM_DIR = r"D:\Sekiro\Output\c0000\Animation\MD_Anims"
UE_BASE = "/Game/Characters/Sekiro/Animations"
CURVES = ["FrameFlags", "CancelActions", "AttackHitbox"]

json_files = sorted(glob.glob(os.path.join(ANIM_DIR, "Anim_Sekiro_a000_*.json")))

total = len(json_files)
full_ok = 0
partial = 0
missing = 0

for i, jp in enumerate(json_files):
    anim_id = os.path.basename(jp).replace("Anim_Sekiro_a000_", "").replace(".json", "")
    asset_name = "Anim_Sekiro_a000_" + anim_id
    asset_path = f"{UE_BASE}/{asset_name}.{asset_name}"

    ok_count = 0
    for curve_name in CURVES:
        cmd = [
            UE_PY, BRIDGE_PY,
            "anim_blueprint", "add_curve",
            asset_path, curve_name,
            "--keys", '[{"time":0.0,"value":0}]',
            "--type", "int",
            "--overwrite", "true",
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            try:
                r = json.loads(result.stdout)
                if r.get("success"):
                    ok_count += 1
            except:
                pass
        # Check for "asset not found" error
        if "not found" in result.stdout.lower() or "not found" in result.stderr.lower():
            missing += 1
            break  # skip rest curves for this anim

    if ok_count == 3:
        full_ok += 1
    elif ok_count > 0:
        partial += 1

    if (i + 1) % 25 == 0 or i == total - 1:
        print(f"[{i+1}/{total}] full={full_ok} partial={partial} missing≈{missing}")

print(f"\nDone!")
print(f"Full curves (all 3): {full_ok}")
print(f"Partial curves: {partial}")
