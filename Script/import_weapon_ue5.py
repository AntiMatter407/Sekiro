"""
Import Kusabimaru weapon FBX into UE5 and create socket on character skeleton.
Run in UE5 Python console (Window > Python Console):
  import sys; sys.path.insert(0, r"F:\ProjectAI\Sekiro\Script")
  import import_weapon_ue5
  import_weapon_ue5.main()

After script completes, manually attach weapon in BP_SekiroCharacter:
  1. Open BP_SekiroCharacter
  2. Add SkeletalMeshComponent as child of Mesh component
  3. Set Skeletal Mesh = SK_WP_A_0300_Kusabimaru
  4. Set Parent Socket = Weapon_Kusabimaru
  5. Compile + Save
"""
import unreal
import os

WEAPON_FBX = r"F:\ProjectAI\Sekiro\Extracted\WP_A_0300_Kusabimaru.fbx"
WEAPON_IMPORT_PATH = "/Game/Weapons/Kusabimaru"
CHAR_SKELETON_PATH = "/Game/Characters/Sekiro/Sekiro_Skeleton"
CHAR_BP_PATH = "/Game/Gameplay/BP_SekiroCharacter"

ATTACH_BONE = "L_Weapon"
SOCKET_NAME = "Weapon_Kusabimaru"


def import_weapon_fbx():
    """Import the weapon FBX as skeletal mesh."""
    unreal.log("=" * 60)
    unreal.log("STEP 1: Import weapon FBX...")
    unreal.log("=" * 60)

    unreal.EditorAssetLibrary.make_directory(WEAPON_IMPORT_PATH)

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", WEAPON_FBX)
    task.set_editor_property("destination_path", WEAPON_IMPORT_PATH)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)

    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("import_animations", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)

    skel_data = unreal.FbxSkeletalMeshImportData()
    skel_data.set_editor_property("import_morph_targets", False)
    skel_data.set_editor_property("update_skeleton_reference_pose", False)
    options.skeletal_mesh_import_data = skel_data

    task.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    imported = list(task.get_editor_property("imported_object_paths"))
    unreal.log(f"Imported {len(imported)} assets:")
    for p in imported:
        unreal.log(f"  {p}")
    return imported


def create_socket_on_character():
    """Create a socket on the character skeleton at L_Weapon bone."""
    unreal.log("\n" + "=" * 60)
    unreal.log("STEP 2: Creating socket on character skeleton...")
    unreal.log("=" * 60)

    skeleton = unreal.load_asset(CHAR_SKELETON_PATH)
    if not skeleton:
        unreal.log_error(f"Skeleton not found: {CHAR_SKELETON_PATH}")
        return False

    # Remove existing socket if present
    existing = skeleton.find_socket(SOCKET_NAME)
    if existing:
        unreal.log(f"Removing existing socket '{SOCKET_NAME}'...")
        skeleton.remove_socket(existing)

    # Find L_Weapon bone
    bone_count = skeleton.get_editor_property("bone_tree")
    bone_index = -1
    # Search through the skeleton bone tree
    for i in range(1000):
        try:
            name = skeleton.get_reference_pose_bone_name(i)
            if name.lower() == ATTACH_BONE.lower():
                bone_index = i
                break
        except Exception:
            break

    if bone_index < 0:
        unreal.log_warning(f"Bone '{ATTACH_BONE}' not found by direct index. Trying alternate method...")
        # Try to find using FReferenceSkeleton
        ref_skel = skeleton.get_reference_skeleton()
        if ref_skel:
            for i in range(ref_skel.get_num()):
                info = ref_skel.get_ref_bone_info(i)
                if info.name.lower() == ATTACH_BONE.lower():
                    bone_index = i
                    unreal.log(f"Found via FReferenceSkeleton: [{i}] {info.name}")
                    break

    if bone_index < 0:
        unreal.log_error(f"Bone '{ATTACH_BONE}' not found in character skeleton!")
        # List all bones for debugging
        unreal.log("Available bones in skeleton:")
        for i in range(200):
            try:
                name = skeleton.get_reference_pose_bone_name(i)
                unreal.log(f"  [{i}] {name}")
            except Exception:
                break
        return False

    unreal.log(f"Found bone '{ATTACH_BONE}' at index {bone_index}")

    # Create socket
    socket = unreal.SkeletalMeshSocket()
    socket.set_editor_property("socket_name", SOCKET_NAME)
    socket.set_editor_property("bone_name", ATTACH_BONE)
    socket.set_editor_property("relative_location", unreal.Vector(0, 0, 0))
    socket.set_editor_property("relative_rotation", unreal.Rotator(0, 0, 0))
    socket.set_editor_property("relative_scale", unreal.Vector(1, 1, 1))

    skeleton.add_socket(socket)
    unreal.EditorAssetLibrary.save_asset(CHAR_SKELETON_PATH)
    unreal.log(f"Socket '{SOCKET_NAME}' created on bone '{ATTACH_BONE}'")
    return True


def main():
    unreal.log("=" * 60)
    unreal.log("KUSABIMARU WEAPON IMPORT")
    unreal.log("=" * 60)

    # Step 1: Import FBX
    imported = import_weapon_fbx()
    if not imported:
        unreal.log_error("FBX import failed! Check file path.")
        return

    # Step 2: Create socket on character skeleton
    if not create_socket_on_character():
        unreal.log_error("Socket creation failed!")
        return

    unreal.log("\n" + "=" * 60)
    unreal.log("AUTOMATED STEPS COMPLETE!")
    unreal.log(f"  Weapon imported to: {WEAPON_IMPORT_PATH}")
    unreal.log(f"  Socket '{SOCKET_NAME}' on bone '{ATTACH_BONE}'")
    unreal.log("")
    unreal.log("MANUAL STEP — Attach weapon to character:")
    unreal.log("  1. Open BP_SekiroCharacter")
    unreal.log("  2. Add SkeletalMeshComponent as child of Mesh")
    unreal.log("  3. Set Skeletal Mesh = the imported weapon")
    unreal.log("  4. Set Parent Socket = Weapon_Kusabimaru")
    unreal.log("  5. Compile + Save")
    unreal.log("=" * 60)


if __name__ == "__main__":
    main()
