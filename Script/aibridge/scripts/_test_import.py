import unreal
import json
json_path = "D:/Sekiro/Output/c0000/Animation/MD_Anims/Anim_Sekiro_a000_100000.json"
with open(json_path, "r", encoding="utf-8") as f:
    data = json.load(f)

clip = data["Animations"][0]
anim_name = clip["Name"]
target_name = "Anim_" + anim_name
frames = clip.get("Frames", [])
frame_count = clip.get("FrameCount", len(frames))
sample_rate = clip.get("SampleRate", 30)

skeleton = unreal.load_asset("/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton")
unreal.log("skeleton: " + str(skeleton))

if not skeleton:
    unreal.log("SKELETON NOT FOUND!")
else:
    # Try getting reference skeleton via raw data
    # Actually, let's try using the AnimSequenceFactory directly
    # which should auto-create with the skeleton's bones
    factory = unreal.AnimSequenceFactory()
    factory.set_editor_property("skeleton", skeleton)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    UE_BASE = "/Game/Characters/Sekiro/Animations"
    existing = UE_BASE + "/" + target_name + "." + target_name
    if unreal.EditorAssetLibrary.does_asset_exist(existing):
        unreal.EditorAssetLibrary.delete_asset(existing)
    anim = tools.create_asset(target_name, UE_BASE, None, factory)
    if anim:
        # Now get the skeleton from the created anim
        skel = anim.get_editor_property("skeleton")
        unreal.log("anim skeleton: " + str(skel))

        # Use data_interface instead of controller
        di = anim.get_editor_property("data_interface")
        unreal.log("data_interface: " + str(type(di)))

        # Or use get_controller()
        controller = anim.get_controller()
        unreal.log("controller: " + str(type(controller)))

        # Try to get bone info from the controller
        controller.open_bracket(unreal.Text("Test"))
        controller.set_frame_rate(unreal.FrameRate(sample_rate, 1))
        controller.set_number_of_frames(frame_count - 1)
        controller.remove_all_bone_tracks()

        # Get skeleton bone names from the reference skeleton
        ref_skel = skel.get_editor_property("reference_skeleton")
        unreal.log("ref_skel: " + str(type(ref_skel)))

        # Try to iterate ref_skel
        num_bones = ref_skel.get_num()
        unreal.log("ref_skel bones: " + str(num_bones))
        for bi in range(min(3, num_bones)):
            bn = ref_skel.get_bone_name(bi)
            unreal.log("  bone[" + str(bi) + "]: " + str(bn))
            # Get ref pose
            rp = ref_skel.get_ref_bone_pose(bi)
            unreal.log("    ref_pose: t=" + str(rp.translation) + " r=" + str(rp.rotation))
    else:
        unreal.log("CREATE FAILED!")
