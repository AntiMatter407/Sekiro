"""
Build Sekiro weapon (楔丸 Kusabimaru) from JSON and export as FBX.
Usage: blender --background --python export_weapon.py
"""
import bpy
import json
import os
import mathutils

JSON_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                         "Extracted", "wp_a_0300_model.json")
TEXTURE_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                           "Extracted", "wp_a_0300-partsbnd-dcx", "parts", "Weapon", "WP_A_0300",
                           "WP_A_0300-tpf")
OUTPUT_FBX = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                          "Extracted", "WP_A_0300_Kusabimaru.fbx")

# UE5 FBX orientation: -Z forward, Y up
AXIS_FORWARD = '-Z'
AXIS_UP = 'Y'
PRIMARY_BONE_AXIS = 'Y'
SCALE = 100.0  # m -> cm


def clear_scene():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for arm in bpy.data.armatures:
        bpy.data.armatures.remove(arm)
    for mesh in bpy.data.meshes:
        bpy.data.meshes.remove(mesh)
    for mat in bpy.data.materials:
        bpy.data.materials.remove(mat)
    for img in bpy.data.images:
        bpy.data.images.remove(img)


def load_image(stem):
    """Load DDS or PNG texture by stem name."""
    for ext in ('.png', '.dds'):
        path = os.path.join(TEXTURE_DIR, stem + ext)
        if os.path.exists(path):
            return bpy.data.images.load(path)
    return None


def convert_to_png(img, png_path):
    """Save loaded DDS as PNG."""
    w, h = img.size
    channels = img.channels
    new_img = bpy.data.images.new(
        name=os.path.basename(png_path), width=w, height=h,
        alpha=(channels >= 4), float_buffer=False,
    )
    new_img.pixels = img.pixels[:]
    new_img.filepath_raw = png_path
    new_img.file_format = 'PNG'
    new_img.save()
    bpy.data.images.remove(new_img)


def build_armature(model_data):
    """Create armature from weapon bone data."""
    arm_data = bpy.data.armatures.new('WeaponArmature')
    arm_obj = bpy.data.objects.new('WeaponSkeleton', arm_data)
    bpy.context.collection.objects.link(arm_obj)
    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode='EDIT')

    bones = model_data['Bones']
    bone_lookup = {}
    created = []

    for b in bones:
        eb = arm_data.edit_bones.new(b['Name'])
        pos = mathutils.Vector(b['WorldPos']) * SCALE
        rot = mathutils.Quaternion((b['WorldRot'][3], b['WorldRot'][0],
                                     b['WorldRot'][1], b['WorldRot'][2]))
        bone_y = rot @ mathutils.Vector((0, 1, 0))
        bone_z = rot @ mathutils.Vector((0, 0, 1))
        eb.head = pos
        eb.tail = pos + bone_y * 0.05 * SCALE
        eb.align_roll(bone_z)
        bone_lookup[b['Name']] = eb
        created.append((b, eb))

    # Set parents
    for b, eb in created:
        if b['ParentName'] and b['ParentName'] in bone_lookup:
            eb.parent = bone_lookup[b['ParentName']]

    bpy.ops.object.mode_set(mode='OBJECT')
    return arm_obj, bone_lookup


def get_texture_for_suffix(available_textures, suffix):
    """Pick best texture for a semantic suffix (_a, _n, _m, _r)."""
    stems = [os.path.splitext(t)[0].lower() for t in available_textures]
    # Try exact suffix match first
    for stem in stems:
        if stem.endswith(suffix):
            return stem
    # Fallback: any texture
    return stems[0] if stems else None


def create_materials(model_data):
    """Create Blender materials for each weapon material slot."""
    materials = []
    for i, mat_data in enumerate(model_data['Materials']):
        mat_name = f"WP_{i}_{mat_data.get('Name', 'unknown')[:30]}"
        mat = bpy.data.materials.new(mat_name)
        mat.use_nodes = True
        nodes = mat.node_tree.nodes
        links = mat.node_tree.links
        nodes.clear()

        bsdf = nodes.new('ShaderNodeBsdfPrincipled')
        bsdf.location = (0, 0)
        output = nodes.new('ShaderNodeOutputMaterial')
        output.location = (300, 0)
        links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

        available = mat_data.get('AvailableTextures', [])

        # Albedo
        albedo_stem = get_texture_for_suffix(available, '_a')
        if albedo_stem:
            img = load_image(albedo_stem)
            if img:
                tex_node = nodes.new('ShaderNodeTexImage')
                tex_node.image = img
                tex_node.location = (-400, 200)
                links.new(tex_node.outputs['Color'], bsdf.inputs['Base Color'])

        # Normal
        normal_stem = get_texture_for_suffix(available, '_n')
        if normal_stem:
            img = load_image(normal_stem)
            if img:
                tex_node = nodes.new('ShaderNodeTexImage')
                tex_node.image = img
                tex_node.location = (-400, -100)
                tex_node.image.colorspace_settings.name = 'Non-Color'
                normal_map = nodes.new('ShaderNodeNormalMap')
                normal_map.location = (-200, -100)
                links.new(tex_node.outputs['Color'], normal_map.inputs['Color'])
                links.new(normal_map.outputs['Normal'], bsdf.inputs['Normal'])

        # Metallic/Roughness
        metal_stem = get_texture_for_suffix(available, '_m')
        if metal_stem:
            img = load_image(metal_stem)
            if img:
                tex_node = nodes.new('ShaderNodeTexImage')
                tex_node.image = img
                tex_node.location = (-400, -400)
                tex_node.image.colorspace_settings.name = 'Non-Color'
                links.new(tex_node.outputs['Color'], bsdf.inputs['Metallic'])
                links.new(tex_node.outputs['Color'], bsdf.inputs['Roughness'])

        materials.append(mat)
    return materials


def build_meshes(model_data, arm_obj, materials):
    """Create mesh objects from weapon mesh data, parented to armature."""
    mesh_objects = []
    all_bone_names = [b['Name'] for b in model_data['Bones']]
    all_bone_indices = {name: idx for idx, name in enumerate(all_bone_names)}

    # Build global bone name list (from FLVER nodes, not the JSON bones subset)
    # The mesh uses FLVER bone indices, so we need the FLVER's own node list.
    # For weapons, the JSON bones include all referenced bones (already filtered).
    # Use the JSON bone list as authoritative.

    for mi, mesh_data in enumerate(model_data['Meshes']):
        mesh = bpy.data.meshes.new(f"Weapon_Mesh_{mi}")
        mesh_obj = bpy.data.objects.new(f"Weapon_Mesh_{mi}", mesh)
        bpy.context.collection.objects.link(mesh_obj)

        # Vertices
        verts = []
        for v in mesh_data['Vertices']:
            pos = mathutils.Vector(v['Pos']) * SCALE
            verts.append(pos)

        # Faces
        faces = []
        for tri in mesh_data['Triangles']:
            faces.append(tuple(tri))

        mesh.from_pydata(verts, [], faces)
        mesh.update()

        # Material
        mat_idx = mesh_data.get('MaterialIndex', 0)
        if mat_idx < len(materials):
            mesh_obj.data.materials.append(materials[mat_idx])

        # Armature modifier
        mod = mesh_obj.modifiers.new('Armature', 'ARMATURE')
        mod.object = arm_obj
        mesh_obj.parent = arm_obj

        # Vertex groups for skinning
        bone_name_map = mesh_data.get('BoneIdxToName', {})
        vg_cache = {}
        for v_idx, v in enumerate(mesh_data['Vertices']):
            for j in range(4):
                w = v['BoneWeights'][j]
                if w <= 0:
                    continue
                bi = v['BoneIndices'][j]
                bone_name = bone_name_map.get(str(bi), all_bone_names[bi] if bi < len(all_bone_names) else None)
                if bone_name is None:
                    continue
                if bone_name not in vg_cache:
                    vg_cache[bone_name] = mesh_obj.vertex_groups.new(name=bone_name)
                vg_cache[bone_name].add([v_idx], w, 'REPLACE')

        mesh_objects.append(mesh_obj)

    return mesh_objects


def export_fbx(filepath, objects):
    """Export selected objects to FBX with UE5-compatible settings."""
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]

    bpy.ops.export_scene.fbx(
        filepath=filepath,
        use_selection=True,
        axis_forward=AXIS_FORWARD,
        axis_up=AXIS_UP,
        primary_bone_axis=PRIMARY_BONE_AXIS,
        global_scale=1.0,
        apply_unit_scale=False,
        bake_space_transform=False,
        object_types={'ARMATURE', 'MESH'},
        use_mesh_modifiers=True,
        add_leaf_bones=False,
        bake_anim=False,
    )
    print(f"FBX exported: {filepath}")


def main():
    print("Loading weapon JSON...")
    with open(JSON_PATH, 'r') as f:
        model_data = json.load(f)

    clear_scene()

    print(f"Bones: {len(model_data['Bones'])}, Meshes: {len(model_data['Meshes'])}, Materials: {len(model_data['Materials'])}")

    print("Building armature...")
    arm_obj, _ = build_armature(model_data)

    print("Creating materials...")
    materials = create_materials(model_data)

    print("Building meshes...")
    mesh_objs = build_meshes(model_data, arm_obj, materials)

    all_objects = [arm_obj] + mesh_objs
    print(f"Exporting to {OUTPUT_FBX}...")
    export_fbx(OUTPUT_FBX, all_objects)

    print("Done!")


if __name__ == '__main__':
    main()
