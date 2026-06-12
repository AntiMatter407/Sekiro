"""
Shared library: build armature, materials, meshes, animations, and export FBX.
Imported by import_model.py and import_anims.py — don't run directly.
"""

import bpy
import os
import json
import mathutils


# ══════════════════════════════════════════════════════════════════════════════
# Utilities
# ══════════════════════════════════════════════════════════════════════════════

def _convert_image_to_png(img, png_path):
    """Convert a loaded Blender image (e.g. DDS) to PNG by copying pixels."""
    w, h = img.size
    channels = img.channels
    pixels = list(img.pixels)  # flat: [R,G,B,A, R,G,B,A, ...]
    new_img = bpy.data.images.new(
        name=os.path.basename(png_path), width=w, height=h,
        alpha=(channels >= 4), float_buffer=False,
    )
    new_img.pixels = pixels
    new_img.filepath_raw = png_path
    new_img.file_format = 'PNG'
    new_img.save()
    bpy.data.images.remove(new_img)
    print(f"  DDS→PNG: {os.path.basename(png_path)}")


# ══════════════════════════════════════════════════════════════════════════════
# Texture scoring / matching
# ══════════════════════════════════════════════════════════════════════════════

def build_texture_cache(texture_root):
    """Build a stem->path cache. PNG preferred over DDS (UE5 compatible)."""
    texture_root = os.path.abspath(texture_root)
    cache = {}
    for root, dirs, files in os.walk(texture_root):
        for f in files:
            low = f.lower()
            if not (low.endswith('.png') or low.endswith('.dds')):
                continue
            stem = os.path.splitext(f)[0].lower()
            path = os.path.normpath(os.path.join(root, f))
            if stem not in cache or low.endswith('.png'):
                cache[stem] = path
    return cache


def strip_texture_suffix(stem):
    for suffix in ('_a', '_n', '_m', '_r'):
        if stem.endswith(suffix):
            return stem[:-2], suffix
    return stem, ''


# Map shader parameter-name keywords to texture semantic suffixes.
# ParamName examples from Sekiro FLVER:
#   "Character_AMSN__DetailBlend__snp_Texture2D_7_AlbedoMap"
#   "Character_AMSN__DetailBlend__snp_Texture2D_0_NormalMap"
#   "Fur_NTC_snp_Texture2D_0_MetallicMap_0"
PARAM_NAME_SEMANTIC_MAP = [
    ('AlbedoMap', '_a'),
    ('DiffuseMap', '_a'),
    ('BaseColorMap', '_a'),
    ('NormalMap', '_n'),
    ('BumpMap', '_n'),
    ('DetailBumpmap', '_n'),
    ('SpecularMap', '_m'),
    ('MetallicMap', '_m'),
    ('ReflectanceMap', '_m'),
    ('RoughnessMap', '_r'),
    ('ShininessMap', '_r'),
    ('AmbientOcclusionMap', '_ao'),
    ('DisplacementMap', '_d'),
    ('EmissiveMap', '_em'),
    ('BloodMask', '_1m'),
    ('SnowMask', '_1m'),
]


def param_name_to_suffix(param_name):
    """Extract texture semantic suffix from a shader parameter name.
    Returns a suffix like '_a', '_n', '_m', '_r' or None."""
    if not param_name:
        return None
    low = param_name.lower()
    for keyword, suffix in PARAM_NAME_SEMANTIC_MAP:
        if keyword.lower() in low:
            return suffix
    return None


def score_texture_candidate(mat_data, stem):
    """Score how well a texture stem matches a material. Returns (score, suffix)."""
    part = mat_data.get('Part', '').lower()
    mat_name = mat_data.get('Name', '').lower()
    mtd_base = os.path.splitext(os.path.basename(mat_data.get('MTD', '').replace('\\', '/')))[0].lower()
    core, suffix = strip_texture_suffix(stem)

    if suffix not in ('_a', '_n', '_m', '_r'):
        return 0, suffix

    if part and not stem.startswith(part.lower()):
        # Face decal materials (HD_*) can use face textures (FC_*)
        mtd_lower = mtd_base.lower()
        if not ('decal' in mtd_lower and stem.startswith('fc_')):
            return 0, suffix

    part_prefix = part.lower() + '_'
    mat_core = mat_name[len(part_prefix):] if mat_name.startswith(part_prefix) else mat_name
    mtd_core = mtd_base[len('p_' + part_prefix):] if mtd_base.startswith('p_' + part_prefix) else mtd_base
    tex_core = core[len(part_prefix):] if core.startswith(part_prefix) else core
    if tex_core.endswith('_new'):
        tex_core = tex_core[:-4]

    haystack = f"{mat_core} {mtd_core}"
    score = 0

    if tex_core and tex_core in haystack:
        score += 100

    ignored = {'p', 'fb', 'm', 'e', 'a', 'cloth', 'material', 'new', '9000', '9510', '00'}
    tex_tokens = [t for t in tex_core.replace('-', '_').split('_') if len(t) >= 2 and t not in ignored]
    hay_tokens = haystack.replace('-', '_').replace('[a]', '').split('_')
    for token in tex_tokens:
        if token in haystack:
            score += 20
        else:
            for ht in hay_tokens:
                if len(token) >= 3 and len(ht) >= 3:
                    if token.startswith(ht) or ht.startswith(token):
                        score += 10
                        break

    # Type-match bonus for FC face/hair parts.
    # Texture types (hair/head/beard/eye) → material MTD types (fur/head/decal/eye).
    # Always applied (not just when score==0) to break ties between FC textures.
    if part and part.startswith('fc_'):
        _synonyms = {
            'hair': ['fur', 'hair'],
            'hair2': ['fur', 'fur2', 'hair'],
            'head': ['head', 'face', 'skin', 'ao'],
            'beard': ['beard', 'decal'],
            'eye': ['eye'],
            'skin': ['skin', 'head', 'face'],
        }
        for syn_key, syn_vals in _synonyms.items():
            if syn_key in tex_core:
                if any(sv in mtd_core for sv in syn_vals):
                    score += 60
                    break

    if score == 0:
        return 0, suffix
    return score, suffix


def assign_textures_globally(materials_data, dds_cache):
    """
    Assign textures to materials. Multiple materials can share the same texture.
    Pass 0: ParamName-based matching from FLVER texture metadata (new C# output).
    Pass 1: score-based greedy assignment (one texture per suffix per material).
    Pass 2: fallback — for materials without textures, assign any available
    textures from the same part, preferring _a (albedo).
    Returns: list of dicts, index matches materials_data.
    """
    suffixes = ('_a', '_n', '_m', '_r')
    result = [{} for _ in materials_data]
    filled_slots = set()

    # ---- Pass 0: ParamName-based matching from FLVER texture metadata ----
    # The new C# output includes "Textures" as a list of {ParamName, Path, ...}
    # and "AvailableTextures" as a list of filenames from the TPF directory.
    for mi, mat_data in enumerate(materials_data):
        flver_tex_list = mat_data.get('Textures', [])
        avail_tex = mat_data.get('AvailableTextures', [])
        # Detect new format: list of dicts with ParamName
        if not isinstance(flver_tex_list, list) or not flver_tex_list:
            continue
        if not isinstance(flver_tex_list[0], dict):
            continue  # old format: {suffix: path} dict

        mat_part = mat_data.get('Part', '').lower()

        for tex_entry in flver_tex_list:
            param = tex_entry.get('ParamName', '')
            suffix = param_name_to_suffix(param)
            if not suffix:
                continue
            slot = (mi, suffix)
            if slot in filled_slots:
                continue

            # Try to find matching texture: first from AvailableTextures,
            # then fall back to dds_cache by part prefix + suffix.
            best_path = None
            # Build a normalized cache of available filenames (stem→full_path)
            avail_stems = {}
            if avail_tex:
                for fn in avail_tex:
                    stem = os.path.splitext(fn)[0].lower()
                    # Map to full path in dds_cache
                    for cache_stem, cache_path in dds_cache.items():
                        if cache_stem == stem:
                            avail_stems[stem] = cache_path
                            break

            # Prefer textures whose stem starts with the material's part prefix
            part_prefix = mat_part + '_' if mat_part else ''
            for stem, path in avail_stems.items():
                if stem.startswith(part_prefix) and stem.endswith(suffix):
                    best_path = path
                    break

            # Fallback: any texture with matching suffix from available list
            if best_path is None:
                for stem, path in avail_stems.items():
                    if stem.endswith(suffix):
                        best_path = path
                        break

            # Last resort: search entire dds_cache by part+suffix
            if best_path is None:
                for stem, path in dds_cache.items():
                    if stem.startswith(part_prefix) and stem.endswith(suffix):
                        best_path = path
                        break

            if best_path:
                result[mi][suffix] = best_path
                filled_slots.add(slot)

    # ---- Pass 1: Score-based greedy assignment (existing heuristic) ----
    candidates = []
    for mi, mat_data in enumerate(materials_data):
        for stem, path in dds_cache.items():
            score, suffix = score_texture_candidate(mat_data, stem)
            if score > 0 and suffix in suffixes:
                candidates.append((score, mi, suffix, path))

    candidates.sort(key=lambda x: -x[0])

    for score, mi, suffix, path in candidates:
        slot = (mi, suffix)
        if slot in filled_slots:
            continue
        result[mi][suffix] = path
        filled_slots.add(slot)

    # Fallback pass
    part_textures = {}
    for stem, path in dds_cache.items():
        core, suffix = strip_texture_suffix(stem)
        if suffix not in suffixes:
            continue
        for mi, mat_data in enumerate(materials_data):
            p = mat_data.get('Part', '').lower()
            if p and stem.startswith(p.lower() + '_'):
                part_textures.setdefault(p, []).append((suffix, path))
                break

    for mi, mat_data in enumerate(materials_data):
        part = mat_data.get('Part', '').lower()
        if not part or part not in part_textures:
            continue
        if result[mi]:
            continue
        available = part_textures[part]
        for preferred in ('_a', '_n', '_m'):
            if (mi, preferred) in filled_slots:
                continue
            for suffix, path in available:
                if suffix == preferred:
                    result[mi][suffix] = path
                    filled_slots.add((mi, suffix))
                    break

    # Decal cross-part override: non-FC face decal materials → FC_* face textures.
    # Overwrites dummy/placeholder textures that HD parts ship with.
    # FC_M parts already have correct textures from scoring — skip them.
    face_cache = [(suffix, path) for stem, path in dds_cache.items()
                  if stem.startswith('fc_') and (suffix := strip_texture_suffix(stem)[1]) in suffixes]
    # head > skin > hair=hair2 > eye > beard > other
    _fc_priority = {'head': 0, 'skin': 1, 'hair': 2, 'hair2': 2, 'eye': 3, 'beard': 4}
    def _fc_sort_key(item):
        suffix, path = item
        stem = os.path.splitext(os.path.basename(path))[0].lower()
        for key, pri in _fc_priority.items():
            if key in stem:
                return pri
        return 5
    face_cache.sort(key=_fc_sort_key)
    if face_cache:
        # Clear only non-FC decal materials (e.g. HD_* parts needing FC_* textures)
        decal_materials = []
        for mi, mat_data in enumerate(materials_data):
            mtd = os.path.basename(mat_data.get('MTD', '').replace('\\', '/')).lower()
            part = mat_data.get('Part', '').lower()
            if 'decal' in mtd and not part.startswith('fc_'):
                decal_materials.append(mi)
                for suffix in list(result[mi].keys()):
                    filled_slots.discard((mi, suffix))
                result[mi].clear()
        # Assign FC textures — first (best) match per suffix wins
        for mi in decal_materials:
            for suffix, path in face_cache:
                slot = (mi, suffix)
                if slot not in filled_slots:
                    result[mi][suffix] = path
                    filled_slots.add(slot)

    return result


def strip_png_alpha(src_path, dst_path=None):
    """Strip alpha channel from an RGBA PNG, return path to new RGB-only PNG."""
    import struct
    import zlib

    if dst_path is None:
        dst_path = src_path.replace('.png', '_rgb.png')
    if os.path.exists(dst_path):
        return dst_path

    with open(src_path, 'rb') as f:
        data = f.read()

    if data[:8] != b'\x89PNG\r\n\x1a\n':
        return src_path

    pos = 8
    new_chunks = [b'\x89PNG\r\n\x1a\n']
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
            if color_type != 6:
                return src_path
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

    raw = zlib.decompress(idat_data)
    new_raw = b''
    offset = 0
    for _ in range(height):
        filt = raw[offset:offset + 1]
        offset += 1
        scan = b''
        for _ in range(width):
            scan += raw[offset:offset + 3]
            offset += 4
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


# ══════════════════════════════════════════════════════════════════════════════
# Material creation
# ══════════════════════════════════════════════════════════════════════════════

CLOTH_KEYWORDS = ('cloth', 'fray', 'tiling', 'bandage', 'muffler', 'rope', 'skirt', 'cape', 'hair')


def _create_hair_material(face_cache, image_cache, get_or_load_image):
    """Create a hair material from FC_* hair textures (not head/skin)."""
    suffixes = ('_a', '_n', '_m', '_r')
    # Create simple material with hair textures
    bmat = bpy.data.materials.new(name='HD_Hair')
    bmat.use_nodes = True
    nodes = bmat.node_tree.nodes
    links = bmat.node_tree.links
    for n in list(nodes):
        nodes.remove(n)
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (400, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])
    bsdf.inputs['Metallic'].default_value = 0.0
    bsdf.inputs['Roughness'].default_value = 0.6
    bmat.use_backface_culling = False

    assigned = set()
    for suffix, path in face_cache:
        if suffix in assigned:
            continue
        try:
            img = get_or_load_image(path)
            node = nodes.new('ShaderNodeTexImage')
            node.image = img
            if suffix == '_a':
                node.location = (-600, 200)
                links.new(node.outputs['Color'], bsdf.inputs['Base Color'])
            elif suffix == '_n':
                node.location = (-600, -100)
                node.image.colorspace_settings.name = 'Non-Color'
                nmap = nodes.new('ShaderNodeNormalMap')
                nmap.location = (-300, -100)
                links.new(node.outputs['Color'], nmap.inputs['Color'])
                links.new(nmap.outputs['Normal'], bsdf.inputs['Normal'])
            assigned.add(suffix)
        except Exception as e:
            print(f"Hair texture load warning: {path}: {e}")
    return bmat


def create_materials(materials_data, texture_root):
    """Create Blender materials, copy used textures to flat Textures/ folder."""
    import shutil

    dds_cache = build_texture_cache(texture_root)
    png_count = sum(1 for v in dds_cache.values() if v.endswith('.png'))
    dds_count = sum(1 for v in dds_cache.values() if v.endswith('.dds'))
    print(f"Found {png_count} PNG + {dds_count} DDS in cache")

    assigned_map = assign_textures_globally(materials_data, dds_cache)
    assigned_albedo = sum(1 for m in assigned_map if '_a' in m)
    assigned_normal = sum(1 for m in assigned_map if '_n' in m)
    assigned_any = sum(1 for m in assigned_map if m)

    # Collect unique textures → flat folder
    texture_root_abs = os.path.abspath(texture_root)
    flat_dir = os.path.join(texture_root_abs, 'Textures')
    os.makedirs(flat_dir, exist_ok=True)

    unique_tex = {}
    for mi in range(len(assigned_map)):
        for suffix, path in assigned_map[mi].items():
            key = os.path.abspath(path)
            unique_tex[key] = suffix

    path_map = {}
    flat_files = {}

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

    for mi in range(len(assigned_map)):
        for suffix in list(assigned_map[mi]):
            old = os.path.abspath(assigned_map[mi][suffix])
            if old in path_map:
                assigned_map[mi][suffix] = path_map[old]

    # Image cache — one Blender data-block per file
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

    # Build a face-cache for hair material fallback
    face_cache = []
    for stem, path in dds_cache.items():
        core, suffix = strip_texture_suffix(stem)
        if suffix in ('_a', '_n', '_m', '_r') and stem.startswith('fc_'):
            face_cache.append((suffix, path))

    # hair > hair2 > other
    _hair_priority = {'hair2': 2, 'hair': 1}  # check hair2 before hair (substring)
    def _hair_sort_key(item):
        stem = os.path.splitext(os.path.basename(item[1]))[0].lower()
        for key, pri in _hair_priority.items():
            if key in stem:
                return pri
        return 2
    face_cache.sort(key=_hair_sort_key)

    # Detect whether any material is a Decal that needs hair splitting
    needs_hair_material = any(
        'decal' in os.path.basename(m.get('MTD', '').replace('\\', '/')).lower()
        for m in materials_data
    )

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

        bsdf.inputs['Metallic'].default_value = 0.0
        bsdf.inputs['Roughness'].default_value = 0.8

        mat_low = mat_name.lower()
        mtd = os.path.basename(mat_data.get('MTD', '').replace('\\', '/')).lower()
        is_decal = 'decal' in mtd

        if any(kw in mat_low for kw in CLOTH_KEYWORDS):
            bmat.use_backface_culling = False

        node_a = None  # Track albedo node for decal alpha hookup
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
                    nmap.location = (-400, -100)
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

        # Blend mode: Decal→BLEND, Hair/Cloth→CLIP, others→OPAQUE
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

    hair_material = None
    if needs_hair_material and face_cache:
        hair_material = _create_hair_material(face_cache, image_cache, get_or_load_image)
        if hair_material:
            blender_materials.append(hair_material)

    # Generate UE5 material config
    mat_config = []
    for mi, bmat in enumerate(blender_materials):
        # HD_Hair is procedurally generated — no MTD, fix config manually
        if bmat.name == 'HD_Hair':
            mat_config.append({
                'name': 'HD_Hair',
                'blend_mode': 'Masked',
                'two_sided': True,
                'notes': 'Procedural hair material',
                'textures': {},
            })
            continue
        mat_data = materials_data[mi] if mi < len(materials_data) else None
        mtd = os.path.basename(mat_data.get('MTD', '').replace('\\', '/')).lower() if mat_data else ''
        mat_low = bmat.name.lower()
        is_decal = 'decal' in mtd
        # Use same CLOTH_KEYWORDS logic as Blender material creation (line 481)
        # to keep blend mode consistent between FBX and UE5 JSON config.
        is_cloth_mat = any(kw in mat_low for kw in CLOTH_KEYWORDS) or ('cloth' in mtd)

        # Prefer MTD-parsed blend mode if available (from C# MTD parsing)
        mtd_info = mat_data.get('MTDInfo', None) if mat_data else None
        if mtd_info:
            mtd_blend = mtd_info.get('BlendMode', '')
            # MTD.BlendMode enum values -> UE5 blend mode
            MTD_BLEND_TO_UE5 = {
                'Normal': 'Opaque',
                'TexEdge': 'Masked',
                'Blend': 'Translucent',
                'Water': 'Translucent',
                'Add': 'Translucent',
                'Sub': 'Translucent',
                'Mul': 'Translucent',
                'LSBlend': 'Translucent',
                'LSAdd': 'Translucent',
            }
            if mtd_blend in MTD_BLEND_TO_UE5:
                blend = MTD_BLEND_TO_UE5[mtd_blend]
            elif is_cloth_mat:
                blend = 'Masked'
            elif is_decal:
                blend = 'Translucent'
            else:
                blend = 'Opaque'
        elif is_cloth_mat:
            blend = 'Masked'
        elif is_decal:
            blend = 'Translucent'
        else:
            blend = 'Opaque'
        cfg = {
            'name': bmat.name,
            'blend_mode': blend,
            'two_sided': is_decal or is_cloth_mat,
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


# ══════════════════════════════════════════════════════════════════════════════
# Armature
# ══════════════════════════════════════════════════════════════════════════════

# Bone chains that need gravity droop (physics bones in bind pose).
# Each entry: (name_pattern, parent_chain_names) — chains are detected by
# walking parent→child links among model-only bones.
# HD_L/R_bone are head decal bones, NOT hair. Sekiro's hair is tied up
# and handled by FC_M_0100 with static fur bones — no gravity needed.
_PHYSICS_BONE_PATTERNS = [
    'bonecloth',
    'Skirt', '_Skirt',
    'Calf_Skirt', 'Thigh_Skirt', 'Knee_Skirt',
]


def _simulate_hair_gravity(arm_obj, model_data):
    """Apply gravity droop to cloth/skirt physics bone chains and bake mesh deformation.

    Uses Verlet chain simulation to compute natural droop target positions,
    then applies POSE rotations to match those targets, bakes into rest pose
    and mesh vertices.

    Must be called AFTER build_meshes so that armature modifiers exist on the
    mesh objects.  The flow is:
      0. Verlet chain simulation → target tail positions
      1. POSE mode: rotate each bone toward its Verlet target (root→tip order)
      2. Apply the armature modifier on every mesh (bakes deformed vertices)
      3. Apply pose as rest pose (armature_apply)
      4. Re-add armature modifiers
    """
    import math

    # ---- Identify physics bones ----
    model_bones = {b['Name'] for b in model_data.get('Bones', [])}
    physics_bones = set()
    for name in model_bones:
        for pat in _PHYSICS_BONE_PATTERNS:
            if pat.lower() in name.lower():
                physics_bones.add(name)
                break
    if not physics_bones:
        return

    # ---- Step 0: Build chains and run Verlet simulation (in OBJECT mode) ----
    bpy.ops.object.mode_set(mode='OBJECT')

    def _find_root(pb):
        while pb.parent and pb.parent.name in physics_bones:
            pb = pb.parent
        return pb.name

    roots = set()
    for name in physics_bones:
        pb = arm_obj.pose.bones.get(name)
        if pb:
            r = _find_root(pb)
            if r:
                roots.add(r)

    if not roots:
        return

    # Build chains: each is (name_list, length_list)
    chains = []
    for root_name in sorted(roots):
        chain_names = []
        cur = root_name
        while cur and cur in physics_bones:
            chain_names.append(cur)
            pb = arm_obj.pose.bones.get(cur)
            if pb is None:
                break
            kids = [c for c in pb.children if c.name in physics_bones]
            cur = kids[0].name if kids else None

        if len(chain_names) < 1:
            continue

        chain_lengths = []
        for name in chain_names:
            bone = arm_obj.data.bones[name]
            chain_lengths.append((bone.tail_local - bone.head_local).length)

        chains.append((chain_names, chain_lengths))

    # Verlet simulation: Y-up in model space, gravity pulls -Y
    GRAVITY = mathutils.Vector((0, -1, 0))
    GRAVITY_SCALE = 0.003
    ITERATIONS = 300

    all_targets = {}  # bone_name → world-space target tail position

    for chain_names, chain_lengths in chains:
        n = len(chain_names)

        root_pb = arm_obj.pose.bones.get(chain_names[0])
        root_head_world = arm_obj.matrix_world @ root_pb.head

        # Initialize particles at current world-space tail positions
        particles = []
        for name in chain_names:
            pb = arm_obj.pose.bones.get(name)
            particles.append(arm_obj.matrix_world @ pb.tail)

        # Verlet integration
        for _ in range(ITERATIONS):
            for i in range(n):
                particles[i] = particles[i] + GRAVITY * GRAVITY_SCALE

            # Constraint: first bone tail at fixed distance from root head
            d0 = particles[0] - root_head_world
            if d0.length > 0.0001:
                particles[0] = root_head_world + d0.normalized() * chain_lengths[0]

            # Constraint: consecutive particles at bone-length distance
            for i in range(1, n):
                d = particles[i] - particles[i - 1]
                if d.length > 0.0001:
                    particles[i] = particles[i - 1] + d.normalized() * chain_lengths[i]

        for i, name in enumerate(chain_names):
            all_targets[name] = particles[i]

    # ---- Step 1: POSE mode — rotate each bone toward its Verlet target ----
    bpy.ops.object.mode_set(mode='POSE')

    for chain_names, _chain_lengths in chains:
        for i, name in enumerate(chain_names):
            pb = arm_obj.pose.bones.get(name)
            if pb is None:
                continue

            # World-space head (FK-affected by parent rotations)
            world_head = arm_obj.matrix_world @ pb.head
            target_tail = all_targets[name]

            desired_dir = (target_tail - world_head).normalized()
            rest_dir = (arm_obj.matrix_world @ pb.bone.vector).normalized()

            rot_quat = rest_dir.rotation_difference(desired_dir)
            pb.rotation_mode = 'QUATERNION'
            pb.rotation_quaternion = rot_quat

    # ---- Step 2: Bake armature modifier → deformed vertices ----
    bpy.ops.object.mode_set(mode='OBJECT')
    mesh_objects = [obj for obj in bpy.data.objects if obj.type == 'MESH']

    for obj in mesh_objects:
        bpy.context.view_layer.objects.active = obj
        arm_mod = None
        for mod in obj.modifiers:
            if mod.type == 'ARMATURE':
                arm_mod = mod
                break
        if arm_mod is None:
            continue
        bpy.ops.object.modifier_apply(modifier=arm_mod.name)

    # ---- Step 3: Bake pose → rest pose on armature ----
    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode='POSE')
    bpy.ops.pose.select_all(action='SELECT')
    bpy.ops.pose.armature_apply(selected=True)
    bpy.ops.object.mode_set(mode='OBJECT')

    # ---- Step 4: Re-add armature modifiers (now rest pose matches drooped mesh) ----
    for obj in mesh_objects:
        mod = obj.modifiers.new('Armature', 'ARMATURE')
        mod.object = arm_obj

    total_bones = sum(len(c[0]) for c in chains)
    print(f"Applied gravity to {len(chains)} physics chain(s) "
          f"({total_bones} bones, {len(mesh_objects)} meshes)")


def build_armature(model_data, anim_data):
    """Build union skeleton: animation bones (HKX rest pose) + model-only bones.
    Returns (armature_object, set_of_created_bone_names)."""
    arm_data = bpy.data.armatures.new('Skeleton')
    arm_obj = bpy.data.objects.new('Sekiro', arm_data)
    bpy.context.collection.objects.link(arm_obj)
    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode='EDIT')

    model_bones = {b['Name']: b for b in model_data['Bones']}
    anim_names = anim_data['BoneNames']
    anim_parents = anim_data['BoneParents']
    anim_locals = anim_data.get('BoneLocalTransforms', [])

    # Compute HKX world transforms for animation skeleton
    anim_world = {}

    def compute_anim_world(i):
        name = anim_names[i]
        if name in anim_world:
            return anim_world[name]
        lt = anim_locals[i]
        loc = mathutils.Vector(lt['P'])
        rot = mathutils.Quaternion((lt['R'][3], lt['R'][0], lt['R'][1], lt['R'][2]))
        sc = mathutils.Vector(lt['S'])
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

    model_only = [n for n in model_bones if n not in set(anim_names)]
    created = {}

    # Create animation bones (HKX rest pose: Y-axis tail, Z-axis roll)
    for i, name in enumerate(anim_names):
        wpos, wrot, _, _ = anim_world[name]
        head = mathutils.Vector(wpos)
        bone_y = wrot @ mathutils.Vector((0, 1, 0))
        tail = head + bone_y * 0.05
        eb = arm_data.edit_bones.new(name)
        eb.head = head
        eb.tail = tail
        bone_z = wrot @ mathutils.Vector((0, 0, 1))
        eb.align_roll(bone_z)
        created[name] = eb

    # Add model-only bones
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

    # Parent model-only bones
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


# ══════════════════════════════════════════════════════════════════════════════
# Meshes
# ══════════════════════════════════════════════════════════════════════════════

def _build_mesh_from_verts(name, verts_list, tris_list, normal_offset=0.0):
    """Build a mesh via BMesh and apply FLVER vertex normals (custom split normals).
    Returns a mesh data-block ready for object creation.
    If normal_offset > 0, vertices are pushed along their normals (for decal z-fighting)."""
    import bmesh
    mesh_data = bpy.data.meshes.new(name)
    bm = bmesh.new()

    # Add vertices (offset along normal for decals)
    bm_verts = []
    for v in verts_list:
        pos = list(v['Pos'])
        if normal_offset:
            n = v.get('Normal', [0, 0, 0])
            pos[0] += n[0] * normal_offset
            pos[1] += n[1] * normal_offset
            pos[2] += n[2] * normal_offset
        bm_verts.append(bm.verts.new(tuple(pos)))
    bm.verts.ensure_lookup_table()
    bm.verts.index_update()

    # Add faces with reversed winding (matches common_blender convention)
    for t in tris_list:
        try:
            bm.faces.new((bm_verts[t[0]], bm_verts[t[2]], bm_verts[t[1]]))
        except ValueError:
            pass  # Duplicate / degenerate face

    bm.faces.ensure_lookup_table()
    bm.to_mesh(mesh_data)
    bm.free()

    # Apply FLVER per-vertex normals via custom split normals
    mesh_data.normals_split_custom_set_from_vertices(
        [tuple(v['Normal']) for v in verts_list]
    )
    mesh_data.update()
    return mesh_data


def _create_mesh_obj(part, mi, verts_list, tris_list, material, arm_obj, created_bones, bone_palette, suffix='', normal_offset=0.0):
    """Create a single mesh object with the given triangles."""
    name = f'{part}_Mesh_{mi}{suffix}'
    mesh_data = _build_mesh_from_verts(name, verts_list, tris_list, normal_offset)
    mesh_obj = bpy.data.objects.new(name, mesh_data)
    bpy.context.collection.objects.link(mesh_obj)
    if material:
        mesh_obj.data.materials.append(material)

    uv_layer = mesh_data.uv_layers.new(name='UVMap')
    for loop in mesh_data.loops:
        uv = verts_list[loop.vertex_index].get('UV', [0, 0])
        uv_layer.data[loop.index].uv = (uv[0], 1.0 - uv[1])
    mesh_data.update()

    vgroups = {}
    for bone_idx, bone_name in bone_palette.items():
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
    return mesh_obj


def build_meshes(model_data, arm_obj, created_bones, materials):
    """Create mesh objects from model data, parented to armature."""
    hair_material = materials[-1] if materials and 'HD_Hair' in getattr(materials[-1], 'name', '') else None
    face_materials = materials[:-1] if hair_material else materials

    for mi, mdata in enumerate(model_data['Meshes']):
        verts_list = mdata['Vertices']
        tris_list = mdata['Triangles']
        if not verts_list or not tris_list:
            continue
        part = mdata.get('Part', 'Part')
        mat_idx = mdata.get('MaterialIndex', 0)
        face_mat = face_materials[mat_idx] if mat_idx < len(face_materials) else None
        palette = {int(k): v for k, v in mdata['BoneIdxToName'].items()}

        # Skip physics-driven cloth meshes (fray/frary) — these
        # are Havok-simulated at runtime and look wrong in the static model.
        # Muffler (scarf) is kept — static mesh that covers the neck.
        if face_mat is not None:
            mat_low = face_mat.name.lower()
            if any(kw in mat_low for kw in ('fray', 'frary')):
                continue

        # Decal meshes: offset vertices slightly along normals to avoid
        # z-fighting with the face mesh they overlay.
        mats_data = model_data.get('Materials', [])
        decal_offset = 0.0
        if mat_idx < len(mats_data):
            mat_data = mats_data[mat_idx]
            mtd = os.path.basename(mat_data.get('MTD', '').replace('\\', '/')).lower()
            if 'decal' in mtd:
                decal_offset = 0.0008  # ~0.8mm, imperceptible but beats z-fighting

        # Split HD parts into face + hair sub-meshes (DISABLED —
        # the hair sub-mesh creates thin strips from HD-bone-weighted tris)
        if False and 'HD_' in part and hair_material:
            hair_bone_indices = set()
            for idx, name in palette.items():
                if 'HD_L_bone' in name or 'HD_R_bone' in name:
                    hair_bone_indices.add(idx)

            if hair_bone_indices:
                hair_tris, face_tris = [], []
                for t in tris_list:
                    hc = sum(1 for vi in t if any(
                        verts_list[vi]['BoneWeights'][j] > 0.1 and verts_list[vi]['BoneIndices'][j] in hair_bone_indices
                        for j in range(4)))
                    if hc >= 2:
                        hair_tris.append(t)
                    else:
                        face_tris.append(t)

                if hair_tris:
                    _create_mesh_obj(part, mi, verts_list, hair_tris,
                                     hair_material, arm_obj, created_bones, palette, '_Hair')
                if face_tris:
                    _create_mesh_obj(part, mi, verts_list, face_tris,
                                     face_mat, arm_obj, created_bones, palette, '_Face')
                continue

        # Non-HD or no hair split needed — single mesh
        _create_mesh_obj(part, mi, verts_list, tris_list,
                         face_mat, arm_obj, created_bones, palette,
                         normal_offset=decal_offset)

        if (mi + 1) % 10 == 0:
            print(f"Built mesh {mi+1}/{len(model_data['Meshes'])}")


# ══════════════════════════════════════════════════════════════════════════════
# Animation
# ══════════════════════════════════════════════════════════════════════════════

def add_actions(arm_obj, anim_data):
    """Create NLA strips from animation data (local-space delta from reference)."""
    anim_bone_names = anim_data['BoneNames']
    ref_locals = anim_data.get('BoneLocalTransforms', [])
    arm_obj.animation_data_create()

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

            # delta_local = A_ref_local⁻¹ @ A_frame_local
            for bone_idx, bone_name in usable:
                t = transforms[bone_idx]
                rt = ref_locals[bone_idx]
                frm_loc = mathutils.Vector(t['P'])
                frm_rot = mathutils.Quaternion((t['R'][3], t['R'][0], t['R'][1], t['R'][2]))
                frm_sc = mathutils.Vector(t['S'])
                ref_loc = mathutils.Vector(rt['P'])
                ref_rot = mathutils.Quaternion((rt['R'][3], rt['R'][0], rt['R'][1], rt['R'][2]))
                ref_sc = mathutils.Vector(rt['S'])
                frm_mat = mathutils.Matrix.LocRotScale(frm_loc, frm_rot, frm_sc)
                ref_mat = mathutils.Matrix.LocRotScale(ref_loc, ref_rot, ref_sc)
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

        # One NLA strip per action
        track = arm_obj.animation_data.nla_tracks.new()
        track.name = anim['Name']
        strip = track.strips.new(anim['Name'], 1, action)
        strip.frame_start = 1
        strip.frame_end = frame_count
        strip.action_frame_start = 1
        strip.action_frame_end = frame_count
        track.mute = False
        arm_obj.animation_data.action = None


# ══════════════════════════════════════════════════════════════════════════════
# Scene helpers
# ══════════════════════════════════════════════════════════════════════════════

def clear_scene():
    """Remove all objects from the current scene."""
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()


def apply_scene_orientation_fix(arm_obj):
    """Create parent Empty to orient character for UE5: upright + facing +X.
    Armature X=90° makes Y-up model upright (Z-up) in Blender.
    Empty Z=180° flips facing so character front points to FBX -Z (=UE5 +X)."""
    import math
    bpy.ops.object.empty_add(type='PLAIN_AXES', location=(0, 0, 0))
    root = bpy.context.active_object
    root.name = 'ExportRoot'
    root.rotation_euler = (0, 0, math.pi)
    arm_obj.parent = root
    arm_obj.rotation_euler = (math.pi / 2, 0, 0)
    return root


FBX_BASE_OPTIONS = {
    'use_selection': True,
    'use_mesh_modifiers': True,
    'mesh_smooth_type': 'EDGE',
    'use_custom_props': False,
    'add_leaf_bones': False,
    'bake_anim_use_all_actions': False,
    'bake_anim_use_nla_strips': True,
    'bake_anim_force_startend_keying': True,
    'path_mode': 'RELATIVE',
    'embed_textures': False,
    'primary_bone_axis': 'Y',
    'secondary_bone_axis': 'X',
    'axis_forward': 'Z',
    'axis_up': 'Y',
}


def export_fbx(filepath, objects, bake_anim=False):
    """Export selected objects to FBX."""
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    print(f"Exporting {filepath}")
    bpy.ops.export_scene.fbx(filepath=filepath, bake_anim=bake_anim, **FBX_BASE_OPTIONS)
