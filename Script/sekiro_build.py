"""
Blender script: Build Sekiro character from model JSON + textures, export FBX.

Usage:
  blender --background --python sekiro_build.py -- Sekiro_model.json Sekiro.fbx [texture_root]

  texture_root: root directory to search for DDS textures (default: same dir as JSON)
"""

import bpy
import sys
import json
import os
import math


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


def find_dds_file(texture_root, texture_path):
    """Find a DDS file matching the FLVER texture path."""
    # FLVER texture paths look like: N:\\GR\\data\\Model\\parts\\...\\tex_name
    tex_name = texture_path.replace("\\", "/").split("/")[-1]
    if not tex_name:
        return None

    # Search recursively for matching DDS
    for root, dirs, files in os.walk(texture_root):
        for f in files:
            if f.lower().endswith(".dds"):
                base = os.path.splitext(f)[0]
                if base.lower() == tex_name.lower():
                    return os.path.join(root, f)
    return None


def build_sekiro(input_json, output_fbx, texture_root=None):
    print(f"Loading JSON: {input_json}")
    with open(input_json, 'r') as f:
        data = json.load(f)

    bones_data = data['Bones']
    meshes_data = data['Meshes']
    materials_data = data.get('Materials', [])

    if texture_root is None:
        texture_root = os.path.dirname(input_json)

    print(f"Bones: {len(bones_data)}")
    print(f"Meshes: {len(meshes_data)}")
    print(f"Materials: {len(materials_data)}")

    # Clear scene
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()
    for c in bpy.data.collections:
        if c.users == 0:
            bpy.data.collections.remove(c)

    # ============================================================
    # STEP 1: Build armature from WorldPos
    # ============================================================
    print("Building armature...")
    arm_data = bpy.data.armatures.new('Skeleton')
    arm_obj = bpy.data.objects.new('Sekiro', arm_data)
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
                if cn not in bone_map:
                    continue
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
                tail_pos = (bhead[0] + d[0]*0.05, bhead[1] + d[1]*0.05, bhead[2] + d[2]*0.05)
        else:
            d = quat_rotate_vector(wq, (0, 1, 0))
            tail_pos = (bhead[0] + d[0]*0.05, bhead[1] + d[1]*0.05, bhead[2] + d[2]*0.05)

        eb = edit_bones.new(name)
        eb.head = bhead
        eb.tail = tail_pos
        created_bones_dict[name] = eb

    # Set parent relationships
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
    print(f"Created {len(created_bones)} bones")

    # ============================================================
    # STEP 2: Build DDS texture cache
    # ============================================================
    print("Scanning for DDS textures...")
    dds_cache = {}
    for root, dirs, files in os.walk(texture_root):
        for f in files:
            if f.lower().endswith(".dds"):
                base = os.path.splitext(f)[0].lower()
                dds_cache[base] = os.path.join(root, f)
    print(f"Found {len(dds_cache)} DDS files")

    # ============================================================
    # STEP 3: Create Blender materials
    # ============================================================
    print("Creating materials...")
    blender_materials = []
    for mi, mat_data in enumerate(materials_data):
        mat_name = mat_data.get('Name', f'Material_{mi}')
        textures = mat_data.get('Textures', {})

        bmat = bpy.data.materials.new(name=mat_name)
        bmat.use_nodes = True
        nodes = bmat.node_tree.nodes
        links = bmat.node_tree.links

        # Clear default nodes
        for n in nodes:
            nodes.remove(n)

        bsdf = nodes.new('ShaderNodeBsdfPrincipled')
        bsdf.location = (0, 0)
        output = nodes.new('ShaderNodeOutputMaterial')
        output.location = (300, 0)
        links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

        # Try to find and assign textures
        for tex_type, tex_path in textures.items():
            tex_name = tex_path.replace("\\", "/").split("/")[-1].lower()
            if tex_name in dds_cache:
                dds_path = dds_cache[tex_name]
                try:
                    img = bpy.data.images.load(dds_path)
                    tex_node = nodes.new('ShaderNodeTexImage')
                    tex_node.image = img

                    # Connect based on texture type suffix
                    tex_lower = tex_type.lower()
                    if 'albedo' in tex_lower or 'diffuse' in tex_lower or tex_lower.startswith('g_diffuse'):
                        tex_node.location = (-400, 200)
                        links.new(tex_node.outputs['Color'], bsdf.inputs['Base Color'])
                    elif 'normal' in tex_lower or 'bumpmap' in tex_lower:
                        tex_node.location = (-400, -100)
                        tex_node.image.colorspace_settings.name = 'Non-Color'
                        normal_map = nodes.new('ShaderNodeNormalMap')
                        normal_map.location = (-200, -100)
                        links.new(tex_node.outputs['Color'], normal_map.inputs['Color'])
                        links.new(normal_map.outputs['Normal'], bsdf.inputs['Normal'])
                    elif 'specular' in tex_lower or 'metallic' in tex_lower or 'reflectance' in tex_lower:
                        tex_node.location = (-400, -400)
                        tex_node.image.colorspace_settings.name = 'Non-Color'
                        links.new(tex_node.outputs['Color'], bsdf.inputs['Metallic'])
                except Exception as e:
                    print(f"  Warning: Could not load texture {dds_path}: {e}")

        blender_materials.append(bmat)

    print(f"Created {len(blender_materials)} materials")

    # ============================================================
    # STEP 4: Create meshes
    # ============================================================
    print("Building meshes...")

    for mi, mdata in enumerate(meshes_data):
        verts_list = mdata['Vertices']
        tris_list = mdata['Triangles']
        bone_palette = {int(k): v for k, v in mdata['BoneIdxToName'].items()}
        mat_idx = mdata.get('MaterialIndex', 0)
        part_name = mdata.get('Part', 'unknown')

        if len(verts_list) == 0 or len(tris_list) == 0:
            continue

        verts_co = [tuple(v['Pos']) for v in verts_list]
        faces = [(t[0], t[1], t[2]) for t in tris_list]

        mesh_name = f'{part_name}_Mesh_{mi}'
        mesh_data = bpy.data.meshes.new(mesh_name)
        mesh_obj = bpy.data.objects.new(mesh_name, mesh_data)
        bpy.context.collection.objects.link(mesh_obj)

        mesh_data.from_pydata(verts_co, [], faces)

        # Assign material
        if mat_idx < len(blender_materials):
            mesh_obj.data.materials.append(blender_materials[mat_idx])

        # UV map
        if any('UV' in v for v in verts_list):
            uv_layer = mesh_data.uv_layers.new(name='UVMap')
            for loop in mesh_data.loops:
                vi = loop.vertex_index
                if vi < len(verts_list):
                    uv = verts_list[vi].get('UV', [0, 0])
                    uv_layer.data[loop.index].uv = (uv[0], 1.0 - uv[1])  # Flip V

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

        if (mi + 1) % 10 == 0:
            print(f"  Built {mi + 1}/{len(meshes_data)} meshes...")

    print(f"Built all {len(meshes_data)} meshes")

    # ============================================================
    # STEP 5: Export FBX
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
        print("Usage: blender --background --python sekiro_build.py -- input.json output.fbx [texture_root]")
    else:
        tex_root = argv[2] if len(argv) >= 3 else None
        build_sekiro(argv[0], argv[1], tex_root)
