import os
import subprocess
import glob

BRIDGE_PY = r"D:\Sekiro\Script\aibridge\bridge.py"
UE_PY = r"D:\Program Files\Epic Games\UE_5.2\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
ANIM_DIR = r"D:\Sekiro\Output\c0000\Animation\MD_Anims"
UE_BASE = "/Game/Characters/Sekiro/Animations"

CURVES = ["FrameFlags", "CancelActions", "AttackHitbox"]

def bridge(args):
    cmd = [UE_PY, BRIDGE_PY] + args
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result

json_files = sorted(glob.glob(os.path.join(ANIM_DIR, "Anim_Sekiro_a000_*.json")))
print(f"Adding curves to {len(json_files)} animations")

success = 0
fail = 0
for i, jp in enumerate(json_files):
    anim_id = os.path.basename(jp).replace("Anim_Sekiro_a000_", "").replace(".json", "")
    asset_name = f"Anim_Sekiro_a000_{anim_id}"
    asset_path = f"{UE_BASE}/{asset_name}.{asset_name}"

    skipped = 0
    for curve_name in CURVES:
        result = bridge([
            "anim_blueprint", "add_curve",
            asset_path, curve_name,
            "--keys", '[{"time":0.0,"value":0}]',
            "--type", "int",
            "--overwrite", "true",
        ])
        if result.returncode == 0:
            try:
                import json
                r = json.loads(result.stdout)
                if r.get("success"):
                    skipped += 1
                else:
                    fail += 1
            except:
                fail += 1
        else:
            fail += 1

    if skipped == 3:
        success += 1

    if (i + 1) % 50 == 0:
        print(f"  [{i+1}/{len(json_files)}] OK so far: {success}/{i+1}")

print(f"Done: {success}/{len(json_files)} animations with all 3 curves")
print(f"Partial failures: {fail}")
