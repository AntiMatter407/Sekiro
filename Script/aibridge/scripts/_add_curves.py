import unreal
import json
import os
import glob

ANIM_DIR = "D:/Sekiro/Output/c0000/Animation/MD_Anims"
UE_BASE = "/Game/Characters/Sekiro/Animations"
CURVES = ["FrameFlags", "CancelActions", "AttackHitbox"]

json_files = sorted(glob.glob(os.path.join(ANIM_DIR, "Anim_Sekiro_a000_*.json")))
unreal.log("Adding curves to " + str(len(json_files)) + " animations")

success = 0
for i, jp in enumerate(json_files):
    anim_id = os.path.basename(jp).replace("Anim_Sekiro_a000_", "").replace(".json", "")
    asset_name = "Anim_Sekiro_a000_" + anim_id
    anim_path = UE_BASE + "/" + asset_name + "." + asset_name

    anim_seq = unreal.load_asset(anim_path)
    if not anim_seq:
        unreal.log_warning("  Can't load: " + asset_name)
        continue

    all_ok = True
    for curve_name in CURVES:
        try:
            # Add curve
            anim_seq.add_curve(curve_name, unreal.AnimationCurveType.ANIM_CURVE_EVALUATION_INTEGER)
            # Set single keyframe
            anim_seq.set_curve_key(0.0, 0, curve_name)
        except Exception as e:
            unreal.log_warning("  " + asset_name + " " + curve_name + ": " + str(e))
            all_ok = False

    if all_ok:
        success += 1
        anim_seq.mark_package_dirty()
        unreal.EditorAssetLibrary.save_asset(anim_path)

    if (i + 1) % 50 == 0:
        unreal.log("[PROGRESS] " + str(i+1) + "/" + str(len(json_files)) + " OK=" + str(success))

unreal.log("DONE: " + str(success) + "/" + str(len(json_files)))
