import unreal
import json
import os
import glob

ANIM_DIR = "D:/Sekiro/Output/c0000/Animation/MD_Anims"
SKELETON_PATH = "/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton"
PREVIEW_MESH_PATH = "/Game/Characters/Sekiro/Sekiro_Model.Sekiro_Model"
UE_BASE_PATH = "/Game/Characters/Sekiro/Animations"

def import_md_anim(json_path):
    try:
        with open(json_path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:
        return "Error reading JSON: " + str(e)

    clip = data["Animations"][0]
    anim_name = clip["Name"]
    target_name = "Anim_" + anim_name

    frames = clip.get("Frames", [])
    sample_rate = clip.get("SampleRate", 30)
    frame_count = clip.get("FrameCount", len(frames))
    bone_names = data["BoneNames"]

    if len(frames) < 2:
        return "Insufficient frames: " + str(len(frames))

    # Load skeleton
    skeleton = unreal.load_asset(SKELETON_PATH)
    if not skeleton:
        return "Skeleton not found at: " + SKELETON_PATH

    preview_mesh = unreal.load_asset(PREVIEW_MESH_PATH)

    # Create/overwrite AnimSequence using Factory
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    # Delete existing
    existing_path = UE_BASE_PATH + "/" + target_name + "." + target_name
    if unreal.EditorAssetLibrary.does_asset_exist(existing_path):
        unreal.log("Deleting existing: " + target_name)
        if not unreal.EditorAssetLibrary.delete_asset(existing_path):
            # Rename to trash and delete
            trash_base = "/Game/_Trash"
            unreal.EditorAssetLibrary.make_directory(trash_base)
            trash_path = trash_base + "/" + target_name
            if unreal.EditorAssetLibrary.rename_asset(existing_path, trash_path):
                unreal.EditorAssetLibrary.delete_asset(trash_path)

    # Create factory
    factory = unreal.AnimSequenceFactory()
    factory.set_editor_property("skeleton", skeleton)

    # Make sure directory exists
    unreal.EditorAssetLibrary.make_directory(UE_BASE_PATH)

    anim_seq = asset_tools.create_asset(target_name, UE_BASE_PATH, None, factory)
    if not anim_seq:
        return "Failed to create asset: " + target_name

    anim_seq.set_editor_property("skeleton", skeleton)
    if preview_mesh:
        anim_seq.set_editor_property("preview_skeletal_mesh", preview_mesh)

    controller = anim_seq.get_controller()
    controller.open_bracket(unreal.Text("Import MD Anim"))
    controller.set_frame_rate(unreal.FrameRate(sample_rate, 1))
    controller.set_number_of_frames(frame_count - 1)
    controller.remove_all_bone_tracks()

    # Build name->index map for skeleton bones
    skel_bones = skeleton.get_editor_property("bone_tree")
    skel_name_to_idx = {}
    for i, sb in enumerate(skel_bones):
        skel_name_to_idx[str(sb.name)] = i

    name_to_anim_idx = {}
    for i, n in enumerate(bone_names):
        name_to_anim_idx[n] = i

    # Per-frame: FK accumulate in anim bone space, then write to skeleton bone space
    # Simple approach: use ref_pose's parent chain to accumulate world transforms
    ref_poses = {}
    for i, sb in enumerate(skel_bones):
        ref_poses[i] = sb.get_ref_pose()

    all_pos = {}
    all_rot = {}
    all_scale = {}

    for fi in range(frame_count):
        frame_bt = frames[fi]["BoneTransforms"]
        for skel_idx in range(len(skel_bones)):
            sb = skel_bones[skel_idx]
            name = str(sb.name)
            anim_idx = name_to_anim_idx.get(name, -1)

            if anim_idx >= 0 and anim_idx < len(frame_bt):
                bt = frame_bt[anim_idx]
                pos = unreal.Vector(bt["P"][0], bt["P"][1], bt["P"][2])
                rot = unreal.Quat(bt["R"][0], bt["R"][1], bt["R"][2], bt["R"][3])
                scale = unreal.Vector(bt.get("S", [1,1,1])[0], bt.get("S", [1,1,1])[1], bt.get("S", [1,1,1])[2])
            else:
                pos = unreal.Vector(0, 0, 0)
                rot = unreal.Quat(0, 0, 0, 1)
                scale = unreal.Vector(1, 1, 1)

            if skel_idx not in all_pos:
                all_pos[skel_idx] = []
                all_rot[skel_idx] = []
                all_scale[skel_idx] = []
            all_pos[skel_idx].append(pos)
            all_rot[skel_idx].append(rot)
            all_scale[skel_idx].append(scale)

    # Write bone tracks
    for skel_idx in range(len(skel_bones)):
        skel_bone_name = str(skel_bones[skel_idx].name)
        if skel_idx in all_pos:
            try:
                controller.add_bone_curve(skel_bone_name)
                controller.set_bone_track_keys(skel_bone_name, all_pos[skel_idx], all_rot[skel_idx], all_scale[skel_idx])
            except Exception as e:
                unreal.log_warning("Bone " + skel_bone_name + ": " + str(e))

    controller.notify_populated()
    controller.close_bracket()
    anim_seq.mark_package_dirty()

    # Save
    unreal.EditorAssetLibrary.save_asset(existing_path)
    unreal.log("Imported: " + target_name)
    return None  # success

def add_curves(anim_name):
    target_name = "Anim_" + anim_name
    asset_path = UE_BASE_PATH + "/" + target_name + "." + target_name
    anim = unreal.load_asset(asset_path)
    if not anim:
        return "Asset not found for curves"
    for curve_name in ["FrameFlags", "CancelActions", "AttackHitbox"]:
        try:
            anim.add_curve(curve_name, unreal.AnimationCurveType.ANIM_CURVE_EVALUATION_INTEGER)
            anim.set_curve_key(0.0, 0, curve_name)
        except Exception as e:
            unreal.log_warning("Curve " + curve_name + ": " + str(e))
    anim.mark_package_dirty()
    unreal.EditorAssetLibrary.save_asset(asset_path)
    return None

# Main
json_files = sorted(glob.glob(os.path.join(ANIM_DIR, "Anim_Sekiro_a000_*.json")))
unreal.log("Found " + str(len(json_files)) + " MD animation JSON files")

imported = 0
failed = 0
for i, json_path in enumerate(json_files):
    anim_id = os.path.basename(json_path).replace("Anim_Sekiro_a000_", "").replace(".json", "")
    anim_name = "Sekiro_a000_" + anim_id
    target_name = "Anim_" + anim_name
    unreal.log("[" + str(i+1) + "/" + str(len(json_files)) + "] " + target_name)

    err = import_md_anim(json_path)
    if err:
        unreal.log_warning("  FAILED: " + err)
        failed += 1
        continue

    unreal.log("  Bones OK")
    err2 = add_curves(anim_name)
    if err2:
        unreal.log_warning("  Curves: " + err2)

    imported += 1
    if (i + 1) % 10 == 0:
        unreal.log("[PROGRESS] " + str(i+1) + "/" + str(len(json_files)) + " imported=" + str(imported) + " failed=" + str(failed))

unreal.log("=== DONE: " + str(imported) + " imported, " + str(failed) + " failed ===")
