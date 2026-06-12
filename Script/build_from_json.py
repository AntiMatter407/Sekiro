"""
Blender script: Build armature + meshes from FLVER JSON.

Two modes:
  1. With reference skeleton FBX (recommended):
       blender --background --python build_from_json.py -- input.json output.fbx skeleton.fbx
     Imports bone positions from a DSAS-exported FBX (correct Y-up space).
     Mesh vertices from the JSON are used as-is (already in Y-up character space).

  2. Legacy / JSON-only mode:
       blender --background --python build_from_json.py -- input.json output.fbx
     Uses pre-computed WorldPos from the JSON directly (may be misaligned because
     the C# tool computes FK in FLVER's internally-rotated coordinate space).
"""

import bpy
import sys
import json
import math
import os


def quat_rotate_vector(q, v):
    """Rotate vector v by quaternion q (w, x, y, z)."""
    qw, qx, qy, qz = q
    qv_w = -qx * v[0] - qy * v[1] - qz * v[2]
    qv_x = qw * v[0] + qy * v[2] - qz * v[1]
    qv_y = qw * v[1] + qz * v[0] - qx * v[2]
    qv_z = qw * v[2] + qx * v[1] - qy * v[0]
    cw, cx, cy, cz = qw, -qx, -qy, -qz
    return (
        qv_x * cw + qv_w * cx + qv_z * cy - qv_y * cz,
        qv_y * cw + qv_w * cy + qv_x * cz - qv_z * cx,
        qv_z * cw + qv_w * cz + qv_y * cx - qv_x * cy,
    )


def build_from_json(input_json, output_fbx, skeleton_fbx=None):
    print(f"Loading JSON: {input_json}")
    with open(input_json, 'r') as f:
        data = json.load(f)

    bones_data = data['Bones']
    meshes_data = data['Meshes']

    print(f"Bones: {len(bones_data)}")
    print(f"Meshes: {len(meshes_data)}")

    # Clear scene
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()

    # ============================================================
    # STEP 1: Build or import armature
    # ============================================================
    if skeleton_fbx:
        # ---------------------------------------------------------
        # Mode 1: import skeleton from DSAS FBX (correct positions)
        # ---------------------------------------------------------
        print(f"Importing skeleton from: {skeleton_fbx}")
        bpy.ops.import_scene.fbx(filepath=skeleton_fbx)

        arm_obj = next(
            (obj for obj in bpy.data.objects if obj.type == 'ARMATURE'), None
        )
        if arm_obj is None:
            raise RuntimeError(f"No armature found in {skeleton_fbx}")

        # Delete any meshes that were imported alongside the skeleton
        for obj in list(bpy.data.objects):
            if obj.type == 'MESH':
                bpy.data.objects.remove(obj, do_unlink=True)

        # Bone names for vertex-group validation
        created_bones = {b.name for b in arm_obj.data.bones}
        print(f"Imported {len(created_bones)} bones from skeleton FBX")

    else:
        # ---------------------------------------------------------
        # Mode 2: build armature from JSON WorldPos (legacy)
        # ---------------------------------------------------------
        arm_data = bpy.data.armatures.new('Skeleton')
        arm_obj = bpy.data.objects.new('Armature', arm_data)
        bpy.context.collection.objects.link(arm_obj)
        bpy.context.view_layer.objects.active = arm_obj
        bpy.ops.object.mode_set(mode='EDIT')

        edit_bones = arm_data.edit_bones
        bone_map = {b['Name']: b for b in bones_data}

        name_to_children = {}
        for b in bones_data:
            pn = b.get('ParentName')
            if pn:
                name_to_children.setdefault(pn, []).append(b['Name'])

        created_bones_dict = {}
        for b in bones_data:
            name = b['Name']
            wp = b['WorldPos']
            wr = b['WorldRot']  # [x, y, z, w]
            wq = (wr[3], wr[0], wr[1], wr[2])  # (w, x, y, z)

            bhead = tuple(wp)

            children = name_to_children.get(name, [])
            if children:
                best_dist = float('inf')
                best_pos = None
                for cn in children:
                    cw = bone_map[cn]['WorldPos']
                    dx, dy, dz = cw[0] - wp[0], cw[1] - wp[1], cw[2] - wp[2]
                    dist = dx*dx + dy*dy + dz*dz
                    if 0.0001 < dist < best_dist:
                        best_dist = dist
                        best_pos = tuple(cw)
                if best_pos:
                    tail_pos = best_pos
                else:
                    d = quat_rotate_vector(wq, (0, 1, 0))
                    tail_pos = (
                        bhead[0] + d[0] * 0.1,
                        bhead[1] + d[1] * 0.1,
                        bhead[2] + d[2] * 0.1,
                    )
            else:
                d = quat_rotate_vector(wq, (0, 1, 0))
                tail_pos = (
                    bhead[0] + d[0] * 0.1,
                    bhead[1] + d[1] * 0.1,
                    bhead[2] + d[2] * 0.1,
                )

            eb = edit_bones.new(name)
            eb.head = bhead
            eb.tail = tail_pos
            created_bones_dict[name] = eb

        master_bone = created_bones_dict.get('Master')
        for b in bones_data:
            name = b['Name']
            if name not in created_bones_dict:
                continue
            parent_name = b.get('ParentName')
            if parent_name and parent_name in created_bones_dict:
                created_bones_dict[name].parent = created_bones_dict[parent_name]
            elif name != 'Master' and master_bone and created_bones_dict[name].parent is None:
                created_bones_dict[name].parent = master_bone

        bpy.ops.object.mode_set(mode='OBJECT')
        created_bones = set(created_bones_dict.keys())

    # ============================================================
    # STEP 2: Create meshes
    # ============================================================
    print("Building meshes...")

    for mi, mdata in enumerate(meshes_data):
        verts_list = mdata['Vertices']
        tris_list = mdata['Triangles']
        bone_palette = {int(k): v for k, v in mdata['BoneIdxToName'].items()}

        if len(verts_list) == 0 or len(tris_list) == 0:
            continue

        # Vertex positions are stored in Y-up character space — use directly.
        verts_co = [tuple(v['Pos']) for v in verts_list]
        faces = [(t[0], t[1], t[2]) for t in tris_list]

        mesh_data = bpy.data.meshes.new(f'Mesh_{mi}')
        mesh_obj = bpy.data.objects.new(f'Mesh_{mi}', mesh_data)
        bpy.context.collection.objects.link(mesh_obj)

        mesh_data.from_pydata(verts_co, [], faces)
        mesh_data.update()

        # Vertex groups
        vgroups = {}
        for bone_idx, bone_name in bone_palette.items():
            if bone_name in created_bones:
                vg = mesh_obj.vertex_groups.new(name=bone_name)
                vgroups[bone_idx] = vg

        # Assign weights
        for vi, vert in enumerate(verts_list):
            for j in range(4):
                bi = vert['BoneIndices'][j]
                bw = vert['BoneWeights'][j]
                if bw > 0 and bi in vgroups:
                    mode = 'REPLACE' if j == 0 else 'ADD'
                    vgroups[bi].add([vi], bw, mode)

        # Armature modifier
        mod = mesh_obj.modifiers.new('Armature', 'ARMATURE')
        mod.object = arm_obj
        mod.use_vertex_groups = True
        mesh_obj.parent = arm_obj

        if (mi + 1) % 5 == 0:
            print(f"  Built {mi + 1}/{len(meshes_data)} meshes...")

    print(f"Built all {len(meshes_data)} meshes")

    # ============================================================
    # Export FBX
    # ============================================================
    bpy.ops.object.select_all(action='DESELECT')
    arm_obj.select_set(True)
    for obj in bpy.data.objects:
        if obj.type == 'MESH':
            obj.select_set(True)

    print(f"Exporting FBX: {output_fbx}")
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
    print("Done!")


if __name__ == "__main__":
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]

    if len(argv) < 2:
        print("Usage: blender --background --python build_from_json.py -- input.json output.fbx [skeleton.fbx]")
    else:
        skeleton = argv[2] if len(argv) >= 3 else None
        build_from_json(argv[0], argv[1], skeleton)
