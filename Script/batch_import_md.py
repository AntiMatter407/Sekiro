import os
import json
import glob
import subprocess

BRIDGE_PY = r"D:\Sekiro\Script\aibridge\bridge.py"
UE_PY = r"D:\Program Files\Epic Games\UE_5.2\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
ANIM_DIR = r"D:\Sekiro\Output\c0000\Animation\MD_Anims"
UE_BASE = "/Game/Characters/Sekiro/Animations"

def bridge(args):
    cmd = [UE_PY, BRIDGE_PY] + args
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result

json_files = sorted(glob.glob(os.path.join(ANIM_DIR, "Anim_Sekiro_a000_*.json")))
print(f"Found {len(json_files)} animations")

for i, jp in enumerate(json_files):
    anim_id = os.path.basename(jp).replace("Anim_Sekiro_a000_", "").replace(".json", "")
    print(f"[{i+1}/{len(json_files)}] Anim_Sekiro_a000_{anim_id}...", end="", flush=True)

    # 1. Import animation using SekiroImport.ImportAnim
    cmd = f"SekiroImport.ImportAnim AnimJson=\"{jp}\" Output=\"{UE_BASE}\""
    result = bridge(["console", cmd])
    if result.returncode != 0 or not result.stdout:
        print(" FAIL")
        continue

    print(" OK")
