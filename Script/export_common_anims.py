"""
Build Sekiro character and export a single FBX containing common animation Actions.

Usage:
  blender --background --python export_common_anims.py -- model.json common_anims.json output.fbx texture_root
"""

import bpy
import sys
import json
import os
import mathutils

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common_blender import (
    _simulate_hair_gravity, _build_mesh_from_verts, _convert_image_to_png,
    build_texture_cache, strip_texture_suffix, build_assigned_map_from_resolved,
)


def quat_rotate_vector(q, v):
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


def strip_png_alpha(src_path, dst_path=None):
    """Strip alpha channel from an RGBA PNG, return path to new RGB-only PNG.
    Uses pure Python (struct+zlib) — no Blender image I/O needed."""
    import struct
    import zlib

    if dst_path is None:
        dst_path = src_path.replace('.png', '_rgb.png')
    if os.path.exists(dst_path):
        return dst_path  # Already processed

    with open(src_path, 'rb') as f:
        data = f.read()

    if data[:8] != b'\x89PNG\r\n\x1a\n':
        return src_path  # Not PNG

    pos = 8
    new_chunks = [b'\x89PNG\r\n\x1a\n']
    color_type = None
    width = height = 0
    idat_data = b''

    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos + 4])[0]
        chunk_type = data[pos + 4:pos + 8]
        chunk_bytes = data[pos + 8:pos + 8 + length]

        if chunk_type == b'IHDR':
            width = struct.unpack('>I', chunk_bytes[0:4])[0]
            height = struct.unpack('>I', chunk_bytes[4:8])[0]
            color_type = chunk_bytes[9]
            if color_type != 6:  # Not RGBA — nothing to strip
                return src_path
            # Rewrite IHDR: color type 6 (RGBA) → 2 (RGB)
            new_ihdr = chunk_bytes[:9] + bytes([2]) + chunk_bytes[10:]
            crc = struct.pack('>I', zlib.crc32(b'IHDR' + new_ihdr) & 0xFFFFFFFF)
            new_chunks.append(struct.pack('>I', len(new_ihdr)) + b'IHDR' + new_ihdr + crc)
        elif chunk_type == b'IDAT':
            idat_data += chunk_bytes
        elif chunk_type == b'IEND':
            break
        else:
            crc = struct.pack('>I', zlib.crc32(chunk_type + chunk_bytes) & 0xFFFFFFFF)
            new_chunks.append(struct.pack('>I', length) + chunk_type + chunk_bytes + crc)

        pos += 12 + length

    # Decompress, drop alpha bytes from each scanline, recompress
    raw = zlib.decompress(idat_data)
    new_raw = b''
    offset = 0
    for _ in range(height):
        filt = raw[offset:offset + 1]
        offset += 1
        scan = b''
        for _ in range(width):
            scan += raw[offset:offset + 3]  # RGB only
            offset += 4  # skip RGBA quad
        new_raw += filt + scan

    new_idat = zlib.compress(new_raw)
    crc = struct.pack('>I', zlib.crc32(b'IDAT' + new_idat) & 0xFFFFFFFF)
    new_chunks.append(struct.pack('>I', len(new_idat)) + b'IDAT' + new_idat + crc)
    crc = struct.pack('>I', zlib.crc32(b'IEND') & 0xFFFFFFFF)
    new_chunks.append(struct.pack('>I', 0) + b'IEND' + crc)

    with open(dst_path, 'wb') as f:
        for c in new_chunks:
            f.write(c)

    return dst_path


def create_materials(materials_data, texture_root):
    import shutil

    dds_cache = build_texture_cache(texture_root)
    print(f"Found {sum(1 for v in dds_cache.values() if v.endswith('.png'))} PNG + "
          f"{sum(1 for v in dds_cache.values() if v.endswith('.dds'))} DDS in cache")

    assigned_map = build_assigned_map_from_resolved(materials_data, dds_cache)
    assigned_albedo = sum(1 for m in assigned_map if '_a' in m)
    assigned_normal = sum(1 for m in assigned_map if '_n' in m)
    assigned_any = sum(1 for m in assigned_map if m)

    # ---- Collect unique textures and copy to flat folder ----
    # Flat folder prevents UE5 import conflicts and ensures simple relative paths.
    texture_root_abs = os.path.abspath(texture_root)
    flat_dir = os.path.join(texture_root_abs, 'Textures')
    os.makedirs(flat_dir, exist_ok=True)

    # Gather unique (abspath) → suffix
    unique_tex = {}
    for mi in range(len(assigned_map)):
        for suffix, path in assigned_map[mi].items():
            key = os.path.abspath(path)
            unique_tex[key] = suffix

    path_map = {}    # old_abs_path → new_flat_path
    flat_files = {}  # basename → full path (dedup by filename)

    for src_abs in sorted(unique_tex):
        basename = os.path.basename(src_abs)
        dst = os.path.join(flat_dir, basename)
        if basename not in flat_files:
            if not os.path.exists(dst):
                shutil.copy2(src_abs, dst)
            flat_files[basename] = dst
        else:
            dst = flat_files[basename]
        path_map[src_abs] = dst

    # Update assigned_map to use flat paths
    for mi in range(len(assigned_map)):
        for suffix in list(assigned_map[mi]):
            old = os.path.abspath(assigned_map[mi][suffix])
            if old in path_map:
                assigned_map[mi][suffix] = path_map[old]

    # Keywords that indicate cloth/fabric materials needing double-sided rendering
    CLOTH_KEYWORDS = ('cloth', 'fray', 'tiling', 'bandage', 'muffler', 'rope', 'skirt', 'cape', 'hair')

    # Image cache: ensure each file is loaded only once (single Blender data-block).
    image_cache = {}

    def get_or_load_image(path):
        abs_path = os.path.abspath(path).replace('\\', '/')
        if abs_path in image_cache:
            return image_cache[abs_path]
        for img in bpy.data.images:
            if img.filepath.replace('\\', '/') == abs_path:
                image_cache[abs_path] = img
                return img
        img = bpy.data.images.load(abs_path)
        image_cache[abs_path] = img
        return img

    blender_materials = []
    for mi, mat_data in enumerate(materials_data):
        mat_name = mat_data.get('Name', f'Material_{mi}')
        bmat = bpy.data.materials.new(name=mat_name)
        bmat.use_nodes = True
        nodes = bmat.node_tree.nodes
        links = bmat.node_tree.links
        for n in list(nodes):
            nodes.remove(n)
        bsdf = nodes.new('ShaderNodeBsdfPrincipled')
        output = nodes.new('ShaderNodeOutputMaterial')
        output.location = (400, 0)
        links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

        # ---- PBR defaults (safe baseline for Sekiro) ----
        bsdf.inputs['Metallic'].default_value = 0.0
        bsdf.inputs['Roughness'].default_value = 0.8

        # ---- Two-sided for cloth-like materials ----
        mat_low = mat_name.lower()
        mtd = os.path.basename(mat_data.get('MTD', '').replace('\\', '/')).lower()
        is_decal = 'decal' in mtd

        if any(kw in mat_low for kw in CLOTH_KEYWORDS):
            bmat.use_backface_culling = False

        node_a = None
        for suffix, tex_path in assigned_map[mi].items():
            try:
                img = get_or_load_image(tex_path)
                # Convert DDS to PNG for UE5 compatibility
                if tex_path.lower().endswith('.dds'):
                    png_path = tex_path[:-4] + '.png'
                    if not os.path.exists(png_path):
                        _convert_image_to_png(img, png_path)
                    assigned_map[mi][suffix] = png_path
                    # Re-load PNG so FBX references .png
                    img = get_or_load_image(png_path)
                node = nodes.new('ShaderNodeTexImage')
                node.image = img
                if suffix == '_a':
                    node.location = (-600, 200)
                    links.new(node.outputs['Color'], bsdf.inputs['Base Color'])
                    node_a = node
                elif suffix == '_n':
                    node.location = (-600, -100)
                    node.image.colorspace_settings.name = 'Non-Color'
                    nmap = nodes.new('ShaderNodeNormalMap')
                    nmap.location = (-300, -100)
                    links.new(node.outputs['Color'], nmap.inputs['Color'])
                    links.new(nmap.outputs['Normal'], bsdf.inputs['Normal'])
                elif suffix == '_m':
                    node.location = (-600, -300)
                    node.image.colorspace_settings.name = 'Non-Color'
                    links.new(node.outputs['Color'], bsdf.inputs['Metallic'])
                elif suffix == '_r':
                    node.location = (-600, -500)
                    node.image.colorspace_settings.name = 'Non-Color'
                    links.new(node.outputs['Color'], bsdf.inputs['Roughness'])
            except Exception as e:
                print(f"Texture load warning: {tex_path}: {e}")

        is_cloth_mat = any(kw in mat_low for kw in CLOTH_KEYWORDS)
        if is_decal and node_a is not None:
            links.new(node_a.outputs['Alpha'], bsdf.inputs['Alpha'])
            bmat.blend_method = 'BLEND'
            bmat.use_backface_culling = False
        elif is_cloth_mat:
            bmat.blend_method = 'CLIP'
            bmat.use_backface_culling = False
            bmat.alpha_threshold = 0.5
        else:
            bmat.blend_method = 'OPAQUE'

        blender_materials.append(bmat)

    # Generate UE5 material config
    CLOTH_KEYWORDS_CFG = ('cloth', 'fray', 'tiling', 'bandage', 'muffler', 'rope', 'skirt', 'cape', 'hair')
    mat_config = []
    for mi, bmat in enumerate(blender_materials):
        mat_data = materials_data[mi] if mi < len(materials_data) else None
        mtd = os.path.basename(mat_data.get('MTD', '').replace('\\', '/')).lower() if mat_data else ''
        is_decal = 'decal' in mtd
        is_cloth_decal = is_decal and 'cloth' in mtd
        is_cloth = 'cloth' in mtd if mtd else False  # Only MTD 'cloth' suffix, fray/hair alone = Opaque
        if is_cloth_decal or is_cloth:
            blend = 'Masked'
        elif is_decal:
            blend = 'Translucent'
        else:
            blend = 'Opaque'
        cfg = {
            'name': bmat.name,
            'blend_mode': blend,
            'two_sided': is_decal or is_cloth,
            'notes': 'Decal overlay - requires alpha blending' if is_decal else '',
        }
        if mat_data:
            cfg['mtd'] = mat_data.get('MTD', '')
            tex_rel = {s: os.path.basename(p) for s, p in assigned_map[mi].items()} if mi < len(assigned_map) else {}
            cfg['textures'] = tex_rel
        mat_config.append(cfg)
    config_path = os.path.join(flat_dir, 'Sekiro_Materials.json')
    with open(config_path, 'w', encoding='utf-8') as f:
        json.dump({'materials': mat_config}, f, indent=2, ensure_ascii=False)
    print(f"Material config: {config_path}")

    print(f"Assigned textures: {assigned_any}/{len(materials_data)} materials (albedo {assigned_albedo}, normal {assigned_normal})")
    return blender_materials


def build_armature(model_data, anim_data):
    arm_data = bpy.data.armatures.new('Skeleton')
    arm_obj = bpy.data.objects.new('Sekiro', arm_data)
    bpy.context.collection.objects.link(arm_obj)
    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode='EDIT')

    model_bones = {b['Name']: b for b in model_data['Bones']}
    anim_names = anim_data['BoneNames']
    anim_parents = anim_data['BoneParents']
    anim_locals = anim_data.get('BoneLocalTransforms', [])

    # Build world transforms for animation skeleton (HKX 146 bones)
    anim_world = {}

    def quat_mul(a, b):
        return mathutils.Quaternion((
            a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
            a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
            a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
            a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        ))

    def compute_anim_world(i):
        name = anim_names[i]
        if name in anim_world:
            return anim_world[name]
        lt = anim_locals[i]
        loc = mathutils.Vector(lt['P'])
        rot = mathutils.Quaternion((lt['R'][3], lt['R'][0], lt['R'][1], lt['R'][2]))
        sc = mathutils.Vector(lt['S'])
        # Use Matrix.LocRotScale — same as add_actions, ensures rest pose
        # and animation world matrices are in the same coordinate space.
        local_mat = mathutils.Matrix.LocRotScale(loc, rot, sc)
        parent = anim_parents[i]
        if parent < 0:
            world_mat = local_mat
        else:
            _, _, _, parent_mat = compute_anim_world(parent)
            world_mat = parent_mat @ local_mat
        pos = world_mat.translation
        rot_q = world_mat.to_quaternion()
        sc_v = world_mat.to_scale()
        res = (pos, rot_q, sc_v, world_mat)
        anim_world[name] = res
        return res

    for i in range(len(anim_names)):
        compute_anim_world(i)

    # Start from full animation skeleton, then add model-only bones => union skeleton
    union_names = list(anim_names)
    model_only = [n for n in model_bones if n not in set(anim_names)]
    union_names.extend(model_only)

    created = {}

    # Child lookup from animation skeleton
    anim_children = {}
    for i, pn in enumerate(anim_parents):
        if pn >= 0:
            anim_children.setdefault(anim_names[pn], []).append(anim_names[i])

    # Create animation bones first — tail direction AND roll must match HKX rest pose
    for i, name in enumerate(anim_names):
        wpos, wrot, _, _ = anim_world[name]
        head = mathutils.Vector(wpos)

        # Tail: point along bone Y axis in HKX space (0,1,0 rotated by wrot)
        bone_y = wrot @ mathutils.Vector((0, 1, 0))
        tail = head + bone_y * 0.05

        eb = arm_data.edit_bones.new(name)
        eb.head = head
        eb.tail = tail
        # Align roll so bone Z axis matches HKX Z axis → rest matrix == HKX reference pose
        bone_z = wrot @ mathutils.Vector((0, 0, 1))
        eb.align_roll(bone_z)
        created[name] = eb

    # Add model-only bones using existing model world positions
    name_to_children = {}
    for b in model_data['Bones']:
        pn = b.get('ParentName')
        if pn:
            name_to_children.setdefault(pn, []).append(b['Name'])

    for name in model_only:
        b = model_bones[name]
        wp = mathutils.Vector(b['WorldPos'])
        wr = b['WorldRot']
        wq = mathutils.Quaternion((wr[3], wr[0], wr[1], wr[2]))
        bone_y = wq @ mathutils.Vector((0, 1, 0))
        bone_z = wq @ mathutils.Vector((0, 0, 1))
        eb = arm_data.edit_bones.new(name)
        eb.head = wp
        eb.tail = wp + bone_y * 0.05
        eb.align_roll(bone_z)
        created[name] = eb

    # Parent animation bones
    for i, name in enumerate(anim_names):
        pn = anim_parents[i]
        if pn >= 0:
            created[name].parent = created[anim_names[pn]]

    # Parent model-only bones using model hierarchy
    master = created.get('Master')
    for b in model_data['Bones']:
        name = b['Name']
        if name not in model_only:
            continue
        pn = b.get('ParentName')
        if pn and pn in created:
            created[name].parent = created[pn]
        elif name != 'Master' and master and created[name].parent is None:
            created[name].parent = master

    bpy.ops.object.mode_set(mode='OBJECT')

    print(f"Union skeleton bones: {len(created)} (anim {len(anim_names)} + model-only {len(model_only)})")
    return arm_obj, set(created.keys())


def build_meshes(model_data, arm_obj, created_bones, materials):
    for mi, mdata in enumerate(model_data['Meshes']):
        verts_list = mdata['Vertices']
        tris_list = mdata['Triangles']
        if not verts_list or not tris_list:
            continue
        part = mdata.get('Part', 'Part')
        mat_idx = mdata.get('MaterialIndex', 0)

        # Decal meshes: offset vertices along normals to beat z-fighting
        mats_data = model_data.get('Materials', [])
        decal_offset = 0.0
        if mat_idx < len(mats_data):
            import os as _os
            mat_data = mats_data[mat_idx]
            mtd = _os.path.basename(mat_data.get('MTD', '').replace('\\', '/')).lower()
            if 'decal' in mtd:
                decal_offset = 0.0008

        mesh_data = _build_mesh_from_verts(f'{part}_Mesh_{mi}', verts_list, tris_list, decal_offset)
        mesh_obj = bpy.data.objects.new(f'{part}_Mesh_{mi}', mesh_data)
        bpy.context.collection.objects.link(mesh_obj)
        if mat_idx < len(materials):
            mesh_obj.data.materials.append(materials[mat_idx])

        uv_layer = mesh_data.uv_layers.new(name='UVMap')
        for loop in mesh_data.loops:
            uv = verts_list[loop.vertex_index].get('UV', [0, 0])
            uv_layer.data[loop.index].uv = (uv[0], 1.0 - uv[1])
        mesh_data.update()

        palette = {int(k): v for k, v in mdata['BoneIdxToName'].items()}
        vgroups = {}
        for bone_idx, bone_name in palette.items():
            if bone_name in created_bones:
                vgroups[bone_idx] = mesh_obj.vertex_groups.new(name=bone_name)
        for vi, vert in enumerate(verts_list):
            for j in range(4):
                bi = vert['BoneIndices'][j]
                bw = vert['BoneWeights'][j]
                if bw > 0 and bi in vgroups:
                    vgroups[bi].add([vi], bw, 'ADD')
        mod = mesh_obj.modifiers.new('Armature', 'ARMATURE')
        mod.object = arm_obj
        mod.use_vertex_groups = True
        mesh_obj.parent = arm_obj
        if (mi + 1) % 10 == 0:
            print(f"Built mesh {mi+1}/{len(model_data['Meshes'])}")


def compute_world_mats(anim_parents, transforms):
    """
    Accumulate local HKX transforms root→leaf to get per-bone world matrices.
    Parents are assumed to have lower indices than their children (standard HKX order).
    """
    n = len(anim_parents)
    world_mats = [None] * n
    for i in range(n):
        t = transforms[i]
        loc = mathutils.Vector(t['P'])
        rot = mathutils.Quaternion((t['R'][3], t['R'][0], t['R'][1], t['R'][2]))
        sc = mathutils.Vector(t['S'])
        local_mat = mathutils.Matrix.LocRotScale(loc, rot, sc)
        p = anim_parents[i]
        world_mats[i] = local_mat if p < 0 else world_mats[p] @ local_mat
    return world_mats


def add_actions(arm_obj, model_data, anim_data):
    anim_bone_names = anim_data['BoneNames']
    anim_parents = anim_data['BoneParents']
    ref_locals = anim_data.get('BoneLocalTransforms', [])
    arm_obj.animation_data_create()

    # Pre-set all used pose bones to QUATERNION mode
    usable = [(i, name) for i, name in enumerate(anim_bone_names) if name in arm_obj.pose.bones]
    print(f"Animation bones matched: {len(usable)}/{len(anim_bone_names)}")
    for _, bone_name in usable:
        arm_obj.pose.bones[bone_name].rotation_mode = 'QUATERNION'

    for anim in anim_data['Animations']:
        action = bpy.data.actions.new(anim['Name'])
        arm_obj.animation_data.action = action
        frame_count = anim['FrameCount']
        print(f"Adding action {anim['Name']} ({frame_count} frames)")
        bpy.context.scene.frame_start = 1
        bpy.context.scene.frame_end = max(bpy.context.scene.frame_end, frame_count)

        for fidx, frame_data in enumerate(anim['Frames']):
            frame_no = fidx + 1
            bpy.context.scene.frame_set(frame_no)
            transforms = frame_data['BoneTransforms']

            # Compute local-space delta from reference pose for each bone.
            # delta_local = A_ref_local⁻¹ @ A_frame_local
            # This is the correct approach — world-space deltas accumulate
            # parent-chain errors that cause finger twitching.
            for bone_idx, bone_name in usable:
                t = transforms[bone_idx]
                rt = ref_locals[bone_idx]
                # Build local matrices
                frm_loc = mathutils.Vector(t['P'])
                frm_rot = mathutils.Quaternion((t['R'][3], t['R'][0], t['R'][1], t['R'][2]))
                frm_sc = mathutils.Vector(t['S'])
                ref_loc = mathutils.Vector(rt['P'])
                ref_rot = mathutils.Quaternion((rt['R'][3], rt['R'][0], rt['R'][1], rt['R'][2]))
                ref_sc = mathutils.Vector(rt['S'])
                frm_mat = mathutils.Matrix.LocRotScale(frm_loc, frm_rot, frm_sc)
                ref_mat = mathutils.Matrix.LocRotScale(ref_loc, ref_rot, ref_sc)
                # delta = ref⁻¹ @ frame
                delta = ref_mat.inverted() @ frm_mat
                loc, rot, sc = delta.decompose()
                pb = arm_obj.pose.bones[bone_name]
                pb.location = loc
                pb.rotation_quaternion = rot
                pb.scale = sc

            for bone_idx, bone_name in usable:
                pb = arm_obj.pose.bones[bone_name]
                pb.keyframe_insert(data_path='location', frame=frame_no)
                pb.keyframe_insert(data_path='rotation_quaternion', frame=frame_no)
                pb.keyframe_insert(data_path='scale', frame=frame_no)

        # One NLA strip per action — no duplicate export
        track = arm_obj.animation_data.nla_tracks.new()
        track.name = anim['Name']
        strip = track.strips.new(anim['Name'], 1, action)
        strip.frame_start = 1
        strip.frame_end = frame_count
        strip.action_frame_start = 1
        strip.action_frame_end = frame_count
        track.mute = False
        arm_obj.animation_data.action = None  # clear active; NLA drives export


def apply_scene_orientation_fix(arm_obj):
    """Create parent Empty to orient character for UE5: upright + facing +X."""
    import math
    bpy.ops.object.empty_add(type='PLAIN_AXES', location=(0, 0, 0))
    root = bpy.context.active_object
    root.name = 'ExportRoot'
    root.rotation_euler = (0, 0, math.pi)
    arm_obj.parent = root
    arm_obj.rotation_euler = (math.pi / 2, 0, 0)
    return root


def export_fbx(filepath, objects, bake_anim=False):
    """Export selected objects to FBX."""
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    print(f"Exporting {filepath}")
    bpy.ops.export_scene.fbx(
        filepath=filepath,
        use_selection=True,
        use_mesh_modifiers=True,
        mesh_smooth_type='EDGE',
        use_custom_props=False,
        add_leaf_bones=False,
        bake_anim=bake_anim,
        bake_anim_use_all_actions=False,
        bake_anim_use_nla_strips=True,
        bake_anim_force_startend_keying=True,
        path_mode='RELATIVE',
        embed_textures=False,
        primary_bone_axis='Y',
        secondary_bone_axis='X',
        axis_forward='Z',
        axis_up='Y',
    )


def main(model_json, anim_json, output_fbx, texture_root):
    print(f"Loading model: {model_json}")
    with open(model_json, 'r', encoding='utf-8') as f:
        model_data = json.load(f)
    print(f"Loading animations: {anim_json}")
    with open(anim_json, 'r', encoding='utf-8') as f:
        anim_data = json.load(f)

    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()

    arm_obj, created_bones = build_armature(model_data, anim_data)
    materials = create_materials(model_data.get('Materials', []), texture_root)
    build_meshes(model_data, arm_obj, created_bones, materials)
    _simulate_hair_gravity(arm_obj, model_data)
    add_actions(arm_obj, model_data, anim_data)
    apply_scene_orientation_fix(arm_obj)

    mesh_objects = [obj for obj in bpy.data.objects if obj.type == 'MESH']

    # ---- FBX 1: Model (skeleton + meshes, no animation) ----
    model_fbx = output_fbx.replace('.fbx', '_Model.fbx')
    export_fbx(model_fbx, [arm_obj] + mesh_objects, bake_anim=False)

    # ---- FBX 2: Animations (skeleton only, with NLA strips) ----
    anim_fbx = output_fbx.replace('.fbx', '_Anim.fbx')
    export_fbx(anim_fbx, [arm_obj], bake_anim=True)

    print("Done")


if __name__ == '__main__':
    argv = sys.argv
    if '--' in argv:
        argv = argv[argv.index('--') + 1:]
    if len(argv) < 4:
        print('Usage: blender --background --python export_common_anims.py -- model.json common_anims.json output.fbx texture_root')
    else:
        main(argv[0], argv[1], argv[2], argv[3])
