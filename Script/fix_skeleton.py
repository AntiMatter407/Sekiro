"""
Blender script: Import flat-bone FBX from MySFformat,
fix bone hierarchy using an authoritative skeleton JSON (from c0000.flver).

Usage:
    blender --background --python fix_skeleton.py -- input.fbx skeleton.json output.fbx
"""

import bpy
import sys
import json
import os


def load_skeleton_hierarchy(json_path):
    """Load bone ParentIndex from a skeleton JSON dump."""
    with open(json_path, 'r') as f:
        data = json.load(f)

    name_to_parent = {}
    index_to_name = {}
    for b in data['Bones']:
        idx = b['Index']
        name = b['Name']
        parent_idx = b['ParentIndex']
        index_to_name[idx] = name

        if parent_idx >= 0 and parent_idx in index_to_name:
            name_to_parent[name] = index_to_name[parent_idx]

    # Second pass to resolve any late-binding indices
    for b in data['Bones']:
        idx = b['Index']
        name = b['Name']
        parent_idx = b['ParentIndex']
        if parent_idx >= 0:
            if parent_idx in index_to_name:
                name_to_parent[name] = index_to_name[parent_idx]

    return name_to_parent


def fix_skeleton(input_fbx, skeleton_json, output_fbx):
    """Import FBX, fix bone hierarchy using skeleton JSON, export FBX."""
    # Load authoritative hierarchy
    if skeleton_json and os.path.exists(skeleton_json):
        bone_parents = load_skeleton_hierarchy(skeleton_json)
        print(f"Loaded {len(bone_parents)} bone parent mappings from skeleton JSON")
    else:
        print("ERROR: Skeleton JSON not found!")
        return

    # Clear scene
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()

    # Import the flat FBX
    bpy.ops.import_scene.fbx(filepath=input_fbx)
    print(f"Imported: {input_fbx}")

    # Find the armature
    armature = None
    for obj in bpy.data.objects:
        if obj.type == 'ARMATURE':
            armature = obj
            break

    if armature is None:
        print("ERROR: No armature found in FBX!")
        return

    print(f"Found armature: {armature.name}")

    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.mode_set(mode='EDIT')

    edit_bones = armature.data.edit_bones
    print(f"Bones in armature: {len(edit_bones)}")

    bone_map = {b.name: b for b in edit_bones}

    fixed = 0
    skipped = 0
    missing_in_skeleton = []
    missing_in_fbx = []

    for bone_name, parent_name in bone_parents.items():
        if bone_name not in bone_map:
            missing_in_fbx.append(bone_name)
            continue

        bone = bone_map[bone_name]

        # Walk up skeleton hierarchy to find an ancestor present in FBX
        ancestor_name = parent_name
        while ancestor_name not in bone_map:
            if ancestor_name in bone_parents:
                ancestor_name = bone_parents[ancestor_name]
            else:
                break

        if ancestor_name not in bone_map:
            missing_in_skeleton.append(f"{bone_name} -> {parent_name} (not in FBX)")
            skipped += 1
            continue

        parent_bone = bone_map[ancestor_name]

        # Only re-parent if currently root-level
        if bone.parent is None:
            bone.parent = parent_bone
            fixed += 1

    # Handle remaining orphan bones: parent to Master if possible
    master_bone = bone_map.get("Master")
    orphans_parented = 0
    for bone in edit_bones:
        if bone.parent is None and bone.name != "Master":
            if master_bone:
                bone.parent = master_bone
                orphans_parented += 1

    bpy.ops.object.mode_set(mode='OBJECT')

    print(f"\nFixed by skeleton: {fixed}")
    print(f"Skipped (parent not in FBX): {skipped}")
    print(f"Orphans parented to Master: {orphans_parented}")

    root_count = sum(1 for b in armature.data.bones if b.parent is None)
    print(f"Remaining root bones: {root_count}")

    if missing_in_skeleton[:5]:
        print(f"Sample missing ancestors: {missing_in_skeleton[:5]}")

    # Export FBX
    bpy.ops.object.select_all(action='DESELECT')
    armature.select_set(True)
    for obj in bpy.data.objects:
        if obj.type == 'MESH':
            obj.select_set(True)

    bpy.ops.export_scene.fbx(
        filepath=output_fbx,
        use_selection=True,
        use_mesh_modifiers=True,
        mesh_smooth_type='FACE',
        add_leaf_bones=False,
        primary_bone_axis='Y',
        secondary_bone_axis='X',
        axis_forward='-Z',
        axis_up='Y',
        bake_anim=False,
    )

    print(f"Exported: {output_fbx}")


if __name__ == "__main__":
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]

    if len(argv) < 3:
        print("Usage: blender --background --python fix_skeleton.py -- input.fbx skeleton.json output.fbx")
    else:
        fix_skeleton(argv[0], argv[1], argv[2])
