"""ModelImporter - Sekiro model import pipeline coordinator.

Responsible for: FLVER/TPF/HKX -> JSON -> Blender FBX -> UE5 SkeletalMesh.
"""

import os, sys, json, re, shutil, subprocess
from pathlib import Path
from typing import Optional

_PROJECT_DIR = Path(__file__).resolve().parent.parent.parent

_ASSET_AUXILIARY_BONES = {
    "Sekiro": [
        {
            "Name": "IK_Foot_Plane",
            "ParentName": "Root",
            "LocalTranslation": [0.0, 0.0, 0.0],
            "LocalRotation": [0.0, 0.0, 0.0, 1.0],
            "LocalScale": [1.0, 1.0, 1.0],
        },
    ],
}


class ModelJsonBuilder:
    """Build complete model JSON from FLVER + MTD + TPF sources."""

    _EDITOR_BONE_PREFIXES = ("Ctrl_", "Collidable_", "BD_Collidable_")
    _EDITOR_BONE_FULL_NAMES = {"MoveSampling", "TwistDummy", "body_T", "Box001"}
    _JP_BONE_PREFIXES = (
        "\u30aa\u30d6\u30b8\u30a7\u30af\u30c8",
        "\u30b9\u30ab\u30fc\u30c8",
        "\u80a9\u4f4d\u7f6e",
        "\u4f4d\u7f6e\u53cd",
        "\u56de\u8ee2\u53cd",
        "\u62bd",
        "\u524d\u5f8c",
        "\u80f8\u90e8\u88dc",
    )
    _FALLBACK_TEXTURE_MAP = {
        "BD_M_9000_body": {"albedo": "BD_M_9000_cloth_a", "normal": "BD_M_9000_cloth_n", "metallic": "BD_M_9000_tops_m"},
        "BD_M_9000_tilingchain": {"albedo": "BD_M_9000_cloth_a", "normal": "BD_M_9000_cloth_n", "metallic": "BD_M_9000_tops_m"},
        "BD_M_9000_tilingrope": {"albedo": "BD_M_9000_cloth_a", "normal": "BD_M_9000_cloth_n", "metallic": "BD_M_9000_tops_m"},
        "BD_M_9000_fray1": {"albedo": "BD_M_9000_cloth_a", "normal": "BD_M_9000_cloth_n", "metallic": "BD_M_9000_tops_m"},
        "BD_M_9000_fraryR_cloth_6": {"albedo": "BD_M_9000_cloth_a", "normal": "BD_M_9000_cloth_n", "metallic": "BD_M_9000_tops_m"},
        "BD_M_9000_fraryR_cloth_15": {"albedo": "BD_M_9000_cloth_a", "normal": "BD_M_9000_cloth_n", "metallic": "BD_M_9000_tops_m"},
    }

    # SPX Shader ? ShaderType enum mapping
    SHADER_TYPE_MAP = {
        "character_amsn.spx":                      "Standard",
        "character_amsn_sss.spx":                  "SSS",
        "fur_ntc.spx":                             "Fur",
        "character_amsn_[detailblend].spx":        "DetailBlend",
        "fur_ntc_cloth.spx":                       "FurCloth",
        "character_amsn_[detailblend]_cloth.spx":  "DetailBlendCloth",
        "character_an_blend_amsn_[fresnel].spx":    "FresnelBlend",
        "character_an_blend_amsn_[fresnel]_cloth.spx": "FresnelBlendCloth",
        "character_amsn_e_sss_cloth.spx":          "SSSCloth",
        "character_amsn_[ao_sss].spx":             "Skin",
        "character_amsn_[cat_eye].spx":            "Eye",
        "character_amsn_cloth.spx":                "Cloth",
    }

    @staticmethod
    def classify_shader(shader_path: str) -> str:
        """Map SPX shader filename to ShaderType enum value."""
        spx_name = os.path.basename(shader_path).lower() if shader_path else ""
        return ModelJsonBuilder.SHADER_TYPE_MAP.get(spx_name, "Standard")

    @staticmethod
    def _is_editor_bone(bone_name):
        if bone_name.startswith(ModelJsonBuilder._EDITOR_BONE_PREFIXES):
            return True
        if bone_name in ModelJsonBuilder._EDITOR_BONE_FULL_NAMES:
            return True
        if bone_name.startswith(ModelJsonBuilder._JP_BONE_PREFIXES):
            return True
        return False

    @staticmethod
    def build(flver_paths, skeleton_flver=None, mtd_root=None, tpf_roots=None,
              asset_name="Sekiro", skeleton_source=None, world_source=None,
              extra_bones=None, drop_bone_weight_threshold=0.5,
              drop_mesh_bone_prefixes=None, texture_lookup_dirs_extra=None,
              original_asset_name=None, auxiliary_bones=None):
        from sekiro_asset_manager.flver_parser import FlverParser, fk_accumulate
        from sekiro_asset_manager.mtd_parser import MtdParser

        if auxiliary_bones is None:
            auxiliary_bones = _ASSET_AUXILIARY_BONES.get(asset_name, [])

        skeleton_data = None
        if skeleton_flver and os.path.exists(skeleton_flver):
            try:
                skeleton_data = FlverParser.parse(skeleton_flver)
            except Exception as e:
                print(f"  [Warning] Skel FLVER parse failed: {e}")

        skeleton_bones = None
        if skeleton_source and os.path.exists(skeleton_source):
            try:
                ext = os.path.splitext(skeleton_source)[1].lower()
                if ext == ".json":
                    with open(skeleton_source, "r", encoding="utf-8") as f:
                        anim_data = json.load(f)
                elif ext == ".hkx":
                    from sekiro_asset_manager.animation_importer import AnimationImporter
                    tmp = os.path.join(os.path.dirname(skeleton_source), "_temp_skel.json")
                    imp = AnimationImporter()
                    if not imp.run_anim_extractor(anibnd_dir=os.path.dirname(skeleton_source), output_json=tmp):
                        raise RuntimeError(f"Extractor failed: {skeleton_source}")
                    with open(tmp, "r", encoding="utf-8") as f:
                        anim_data = json.load(f)
                else:
                    raise ValueError(f"Bad skeleton_source type: {ext}")
                bn = anim_data.get("BoneNames", [])
                bp = anim_data.get("BoneParents", [])
                bl = anim_data.get("BoneLocalTransforms", [])
                if not bn and anim_data.get("Bones"):
                    skeleton_bones = anim_data["Bones"]
                elif bn and bp and bl:
                    if len(bp) != len(bn) or len(bl) != len(bn):
                        raise ValueError("Skeleton data length mismatch")
                    skeleton_bones = fk_accumulate(bn, bp, bl)
            except Exception as e:
                print(f"  [Warning] skeleton_source failed: {e}, falling back to FLVER merge")
                skeleton_bones = None

        body_flvers = []
        _use_global_material_index = False
        if skeleton_flver and os.path.exists(skeleton_flver):
            hkx = skeleton_source if (skeleton_source and skeleton_source.endswith(".hkx")) else None
            combined = FlverParser.parse_all(flver_paths, skeleton_flver, skeleton_hkx=hkx)
            gb = combined.get("Bones", [])
            gm = combined.get("Materials", [])
            gms = combined.get("Meshes", [])
            gd = combined.get("Dummies", [])
            gbb = combined.get("BoundingBox", {"Min": [0, 0, 0], "Max": [0, 0, 0]})
            pmap, mmap = {}, {}
            for i, m in enumerate(gms):
                mp = m.get("Part", "") or "part_" + str(i)
                pmap.setdefault(mp, []).append(m)
                mmap.setdefault(mp, [])
                mi = m.get("MaterialIndex", 0)
                if mi < len(gm):
                    mat = gm[mi]
                    if not any(x.get("Name") == mat.get("Name") for x in mmap[mp]):
                        mmap[mp].append(dict(mat))
            for fp in flver_paths:
                pn = os.path.splitext(os.path.basename(fp))[0]
                ms = pmap.get(pn, [])
                mt = mmap.get(pn, [])
                if not ms and pmap:
                    k0 = next(iter(pmap))
                    ms = pmap[k0]
                    mt = mmap[k0]
                body_flvers.append({"FileName": os.path.basename(fp), "Bones": gb,
                                    "Materials": mt, "Meshes": ms,
                                    "Dummies": gd, "BoundingBox": gbb})
            _use_global_material_index = True
        else:
            for fp in flver_paths:
                body_flvers.append(FlverParser.parse(fp))

        if not body_flvers:
            raise ValueError("No valid FLVER files to parse")

        if skeleton_bones is not None:
            merged = list(skeleton_bones)
            for i, b in enumerate(merged):
                b["HkxIndex"] = i
            # --- Step A: Overwrite shared bones' WorldPos with FLVER model bone positions ---
            flver_bone_map = {}
            for fv in body_flvers:
                for b in fv.get("Bones", []):
                    flver_bone_map[b["Name"]] = b
            overwritten = 0
            for b in merged:
                if b["Name"] in flver_bone_map:
                    fb = flver_bone_map[b["Name"]]
                    b["WorldPos"] = fb.get("WorldPos", b.get("WorldPos", [0,0,0]))
                    b["WorldRot"] = fb.get("WorldRot", b.get("WorldRot", [0,0,0,1]))
                    b["WorldScale"] = fb.get("WorldScale", b.get("WorldScale", [1,1,1]))
                    overwritten += 1
            # --- Step B: FK accumulate HKX-only bones ---
            from sekiro_asset_manager.skeleton_merge import _quat_mul, _quat_rotate
            fk_count = 0
            for i, b in enumerate(merged):
                if b["Name"] in flver_bone_map:
                    continue
                local_p = b.get("LocalPos", [0,0,0])
                local_s = b.get("LocalScale", [1,1,1])
                parent_idx = b.get("ParentIndex", -1)
                parent_wp = [0,0,0]; parent_wr = [0,0,0,1]; parent_ws = [1,1,1]
                if parent_idx >= 0 and parent_idx < len(merged):
                    p = merged[parent_idx]
                    parent_wp = p.get("WorldPos", [0,0,0])
                    parent_wr = p.get("WorldRot", [0,0,0,1])
                    parent_ws = p.get("WorldScale", [1,1,1])
                local_scaled = [local_p[j] * parent_ws[j] for j in range(3)]
                rotated_offset = _quat_rotate(parent_wr, local_scaled)
                b["WorldPos"] = [round(parent_wp[j] + rotated_offset[j], 10) for j in range(3)]
                local_r = b.get("LocalRot", [0,0,0,1])
                b["WorldRot"] = [round(v, 10) for v in (_quat_mul(parent_wr, local_r) if len(local_r)==4 else parent_wr)]
                b["WorldScale"] = [parent_ws[j] * local_s[j] for j in range(3)]
                fk_count += 1
            for i, b in enumerate(merged):
                parent_idx = b.get("ParentIndex", -1)
                if parent_idx >= 0 and parent_idx < len(merged):
                    p = merged[parent_idx]
                    pw = p.get("WorldPos", [0,0,0]); bw = b.get("WorldPos", [0,0,0])
                    b["LocalPos"] = [round(bw[j] - pw[j], 10) for j in range(3)]
                else:
                    b["LocalPos"] = list(b.get("WorldPos", [0,0,0]))
                b["LocalRot"] = list(b.get("WorldRot", [0,0,0,1]))
                b["LocalScale"] = list(b.get("WorldScale", [1,1,1]))
            print(f"  Merge: {overwritten} bones WorldPos overwritten, {fk_count} FK-accumulated")
            skinned = set()
            for fv in body_flvers:
                for m in fv.get("Meshes", []):
                    for nm in m.get("BoneIdxToName", {}).values():
                        skinned.add(nm)
            base = {b["Name"] for b in skeleton_bones}
            extra = skinned - base
            if extra_bones:
                extra.update(extra_bones)
            print(f"  [TRACE] extra bones count: {len(extra)}")
            if False and extra:
                fmap = {}
                for fv in body_flvers:
                    for b in fv.get("Bones", []):
                        fmap[b["Name"]] = b
                pend = sorted(extra - base)
                stuck = 0
                while pend:
                    added = False
                    retry = []
                    for bn in pend:
                        if bn not in fmap:
                            continue
                        pn = fmap[bn].get("ParentName", "") or ""
                        if pn and pn not in base:
                            retry.append(bn)
                            continue
                        bd = dict(fmap[bn])
                        if pn:
                            for j, e in enumerate(merged):
                                if e["Name"] == pn:
                                    bd["ParentIndex"] = j
                                    break
                        else:
                            bd["ParentIndex"] = -1
                        merged.append(bd)
                        base.add(bn)
                        added = True
                    pend = retry
                    if not added:
                        stuck += 1
                        if stuck > 3:
                            for bn in pend:
                                bd = dict(fmap[bn])
                                bd["ParentIndex"] = 0
                                merged.append(bd)
                                
                            break
                # --- Re-derive Local for appended ModelOnly bones ---
                appended_start = len(merged) - len(extra)
                if appended_start < len(merged):
                    from sekiro_asset_manager.skeleton_merge import _quat_mul, _quat_rotate
                    for i in range(appended_start, len(merged)):
                        bi = merged[i]
                        local_p = bi.get("LocalPos", [0,0,0])
                        local_s = bi.get("LocalScale", [1,1,1])
                        parent_idx = bi.get("ParentIndex", -1)
                        if parent_idx >= 0 and parent_idx < len(merged):
                            p = merged[parent_idx]
                            pw = p.get("WorldPos", [0,0,0])
                            pr = p.get("WorldRot", [0,0,0,1])
                            ps = p.get("WorldScale", [1,1,1])
                            local_scaled = [local_p[j] * ps[j] for j in range(3)]
                            rotated_offset = _quat_rotate(pr, local_scaled)
                            bi["WorldPos"] = [round(pw[j] + rotated_offset[j], 10) for j in range(3)]
                            local_r = bi.get("LocalRot", [0,0,0,1])
                            bi["WorldRot"] = [round(v, 10) for v in (_quat_mul(pr, local_r) if len(local_r)==4 else pr)]
                            bi["WorldScale"] = [ps[j] * local_s[j] for j in range(3)]
                        else:
                            bi["WorldPos"] = list(local_p)
                            bi["WorldRot"] = list(bi.get("LocalRot", [0,0,0,1]))
                            bi["WorldScale"] = list(local_s)
                    # Re-derive Local from World for appended bones
                    for i in range(appended_start, len(merged)):
                        bi = merged[i]
                        parent_idx = bi.get("ParentIndex", -1)
                        if parent_idx >= 0 and parent_idx < len(merged):
                            p = merged[parent_idx]
                            pw = p.get("WorldPos", [0,0,0])
                            bw = bi.get("WorldPos", [0,0,0])
                            bi["LocalPos"] = [round(bw[j] - pw[j], 10) for j in range(3)]
                        else:
                            bi["LocalPos"] = list(bi.get("WorldPos", [0,0,0]))
                        bi["LocalRot"] = list(bi.get("WorldRot", [0,0,0,1]))
                        bi["LocalScale"] = list(bi.get("WorldScale", [1,1,1]))
        else:
            # skeleton_flver is a file path, need to parse it first
            skel_dict = None
            if skeleton_flver and os.path.exists(skeleton_flver):
                try:
                    skel_dict = FlverParser.parse(skeleton_flver)
                except Exception:
                    pass
            merged = ModelJsonBuilder._merge_bones(skel_dict, body_flvers)
            extra = set()
            # Derive ParentIndex from ParentName (FLVER bones only have names)
            _name_to_idx = {}
            for _i, _b in enumerate(merged):
                _name_to_idx[_b.get("Name", "")] = _i
            for _b in merged:
                _pn = _b.get("ParentName", "") or ""
                _b["ParentIndex"] = _name_to_idx.get(_pn, -1)
            # Topological sort: root first, then parents before children
            _sorted = []
            _pending = list(merged)
            _seen_names = set()
            _stuck = 0
            while _pending and _stuck < 10:
                _added = False
                _retry = []
                for _b in _pending:
                    _pn = _b.get("ParentName", "") or ""
                    if not _pn or _pn in _seen_names:
                        _sorted.append(_b)
                        _seen_names.add(_b.get("Name", ""))
                        _added = True
                    else:
                        _retry.append(_b)
                _pending = _retry
                if not _added:
                    _stuck += 1
                    # Force-add remaining as children of root
                    for _b in _pending:
                        _b["ParentName"] = ""
                        _sorted.append(_b)
                    _pending = []
            merged = _sorted
            # Recompute ParentIndex after sort
            _name_to_idx2 = {}
            for _i, _b in enumerate(merged):
                _name_to_idx2[_b.get("Name", "")] = _i
            for _b in merged:
                _pn = _b.get("ParentName", "") or ""
                _b["ParentIndex"] = _name_to_idx2.get(_pn, -1)
        _tr = {b["Name"]: b["WorldPos"][1] for b in merged if b["Name"] in ("L_Foot","Head")}
        print(f"  [TRACE] Before output: L_Foot=%.2f Head=%.2f extra_count=%d" % (_tr.get("L_Foot",0), _tr.get("Head",0), len(extra)))

        bn2idx = {b["Name"]: i for i, b in enumerate(merged)}

        tpf_roots = tpf_roots or []
        part_texture_map = {}
        for fp in flver_paths:
            pn = os.path.splitext(os.path.basename(fp))[0]
            all_tex_dirs = list(tpf_roots) + (texture_lookup_dirs_extra if texture_lookup_dirs_extra else [])
            part_texture_map[pn] = ModelJsonBuilder._collect_available_textures(all_tex_dirs, pn)

        all_materials_raw = []
        for fv in body_flvers:
            fn = fv.get("FileName", "")
            pn = os.path.splitext(fn)[0]
            pa = part_texture_map.get(pn, [])
            for m in fv.get("Materials", []):
                me = dict(m)
                me["Part"] = pn
                me["AvailableTextures"] = pa
                all_materials_raw.append(me)

        materials_with_mtd = ModelJsonBuilder._resolve_mtd_for_materials(all_materials_raw, mtd_root)
        # Build texture lookup dirs: tpf_roots + unified Output/Textures/
        texture_lookup_dirs = list(tpf_roots or [])
        # Look in Output/Textures/{orig}/ + Output/Textures/Shared/
        orig_name = original_asset_name or asset_name
        asset_tex_dir = os.path.join(_PROJECT_DIR, "Output", "Textures", orig_name)
        shared_tex_dir = os.path.join(_PROJECT_DIR, "Output", "Textures", "Shared")
        for d in [asset_tex_dir, shared_tex_dir]:
            if os.path.isdir(d) and d not in texture_lookup_dirs:
                texture_lookup_dirs.append(d)
        resolved_materials = ModelJsonBuilder._build_resolved_materials(materials_with_mtd, mtd_root, tpf_roots, texture_lookup_dirs)
        
        _merged_mat_name_to_idx = {}
        for _idx, _m in enumerate(all_materials_raw):
            _merged_mat_name_to_idx[_m.get("Name", "")] = _idx
        mat_index_offset = 0
        all_meshes = []
        for fv in body_flvers:
            for mesh in fv.get("Meshes", []):
                fm2 = dict(mesh)
                _raw_mi = mesh.get("MaterialIndex", 0)
                if _use_global_material_index:
                    _mat_name = gm[_raw_mi].get("Name", "") if _raw_mi < len(gm) else ""
                    fm2["MaterialIndex"] = _merged_mat_name_to_idx.get(_mat_name, _raw_mi)
                else:
                    fm2["MaterialIndex"] = _raw_mi + mat_index_offset
                verts = fm2.get("Vertices", [])
                bm = mesh.get("BoneIdxToName", {})
                _drop = False
                if drop_mesh_bone_prefixes:
                    _prefixes = tuple(drop_mesh_bone_prefixes)
                    _drop = any(nm.upper().startswith(_prefixes) for nm in bm.values())
                if _drop:
                    continue
                local_to_final = {}
                new_bone_idx_to_name = {}
                for k, nm in bm.items():
                    li = int(k)
                    if nm in bn2idx:
                        fi = bn2idx[nm]
                        local_to_final[li] = fi
                        new_bone_idx_to_name[str(fi)] = nm
                for v in verts:
                    oi = v.get("BoneIndices", [0, 0, 0, 0])
                    v["BoneIndices"] = [local_to_final.get(i, i) for i in oi]
                fm2["BoneIdxToName"] = new_bone_idx_to_name
                fm2["Vertices"] = verts
                all_meshes.append(fm2)
            if not _use_global_material_index: mat_index_offset += len(fv.get("Meshes", []))

        all_dummies = []
        for fv in body_flvers:
            all_dummies.extend(fv.get("Dummies", []))

        overall_bb = {"Min": [0.0, 0.0, 0.0], "Max": [0.0, 0.0, 0.0]}
        bbs = [fv.get("BoundingBox") for fv in body_flvers if fv.get("BoundingBox")]
        if bbs:
            overall_bb = {
                "Min": [min(v["Min"][i] for v in bbs) for i in range(3)],
                "Max": [max(v["Max"][i] for v in bbs) for i in range(3)],
            }

        name_counts = {}
        for m in materials_with_mtd:
            name_counts[m.get("Name", "")] = name_counts.get(m.get("Name", ""), 0) + 1
        part_name_counts = {}
        for m in materials_with_mtd:
            key = m.get("Part", "") + "|" + m.get("Name", "")
            part_name_counts[key] = part_name_counts.get(key, 0) + 1

        merged_materials = []
        for idx, m in enumerate(materials_with_mtd):
            mg = dict(m)
            old_name = m.get("Name", "")
            part = m.get("Part", "")
            is_dup = name_counts.get(old_name, 0) > 1
            if is_dup:
                part_key = part + "|" + old_name
                part_is_dup = part_name_counts.get(part_key, 0) > 1
                if part_is_dup:
                    mg["Name"] = part + "_" + old_name + "_" + str(idx)
                else:
                    mg["Name"] = part + "_" + old_name
            matched_rm = None
            for rm in resolved_materials:
                if rm.get("Name") == old_name and rm.get("Part") == part:
                    matched_rm = rm
                    break
            if not matched_rm:
                for rm in resolved_materials:
                    if rm.get("Name") == old_name:
                        matched_rm = rm
                        break
            if matched_rm:
                for key in ("ResolvedBlendMode", "TwoSided", "IsCloth", "IsHair", "IsFur", "IsDecal", "DrawStep"):
                    dv = "Opaque" if key in ("DrawStep", "ResolvedBlendMode") else False
                    mg[key] = matched_rm.get(key, dv)
                mg["Textures"] = matched_rm.get("Textures", {})
            merged_materials.append(mg)

        # Filter unused materials (e.g., HD_ meshes dropped by drop_mesh_bone_prefixes)
        used_mat_indices = set()
        for m in all_meshes:
            used_mat_indices.add(m.get("MaterialIndex", 0))
        
        # Filter merged_materials to only used indices
        old_to_new_idx = {}
        new_merged_materials = []
        for old_idx, m in enumerate(merged_materials):
            if old_idx in used_mat_indices:
                old_to_new_idx[old_idx] = len(new_merged_materials)
                new_merged_materials.append(m)
        merged_materials = new_merged_materials
        
        # Remap MaterialIndex in meshes
        for m in all_meshes:
            old_mi = m.get("MaterialIndex", 0)
            if old_mi in old_to_new_idx:
                m["MaterialIndex"] = old_to_new_idx[old_mi]
            else:
                m["MaterialIndex"] = 0
        
        # Filter unused bones from skeleton (HD_ bones from dropped meshes)
        if drop_mesh_bone_prefixes:
            used_bone_names = set()
            for m in all_meshes:
                for nm in m.get("BoneIdxToName", {}).values():
                    used_bone_names.add(nm)
            name_to_bone = {b["Name"]: b for b in merged}
            for nm in list(used_bone_names):
                cur = nm
                while cur:
                    used_bone_names.add(cur)
                    b = name_to_bone.get(cur)
                    cur = b.get("ParentName", "") if b else ""
            new_merged = [dict(b) for b in merged if b["Name"] in used_bone_names]
            name_to_new_idx = {b["Name"]: i for i, b in enumerate(new_merged)}
            # Remap ParentIndex in filtered bones
            for b in new_merged:
                old_pn = b.get("ParentName", "") or ""
                b["ParentIndex"] = name_to_new_idx.get(old_pn, -1)
            name_to_new_idx = {b["Name"]: i for i, b in enumerate(new_merged)}
            for m in all_meshes:
                old_bm = m.get("BoneIdxToName", {})
                new_bm = {}
                for old_i_str, nm in old_bm.items():
                    if nm in name_to_new_idx:
                        new_bm[str(name_to_new_idx[nm])] = nm
                m["BoneIdxToName"] = new_bm
                # Remap bone indices in vertex data
                old_to_new_bone_idx = {}
                for old_idx_str, nm in old_bm.items():
                    if nm in name_to_new_idx:
                        old_to_new_bone_idx[int(old_idx_str)] = name_to_new_idx[nm]
                for v in m.get("Vertices", []):
                    if "BoneIndices" in v:
                        v["BoneIndices"] = [old_to_new_bone_idx.get(bi, 0) for bi in v["BoneIndices"]]
            merged = new_merged

        return {
            "FileName": asset_name + "_model.json",
            "AssetName": asset_name,
            "OriginalAssetName": original_asset_name or asset_name,
            "SkeletonName": asset_name + "_Skeleton",
            "Bones": merged,
            "AuxiliaryBones": list(auxiliary_bones or []),
            "Materials": merged_materials,
            "ResolvedMaterials": resolved_materials,
            "Meshes": all_meshes,
            "Dummies": all_dummies,
            "BoundingBox": overall_bb,
        }

    @staticmethod
    def _merge_bones(skeleton_flver, body_flvers):
        merged = []
        def _keep(b):
            return not ModelJsonBuilder._is_editor_bone(b["Name"])
        if skeleton_flver:
            merged = [b for b in skeleton_flver.get("Bones", []) if _keep(b)]
            sn = {b["Name"] for b in merged}
            for fv in body_flvers:
                for b in fv.get("Bones", []):
                    if b["Name"] not in sn and _keep(b):
                        merged.append(b)
                        sn.add(b["Name"])
            if body_flvers:
                merged = [b for b in body_flvers[0].get("Bones", []) if _keep(b)]
                mn = {b["Name"] for b in merged}
                for fv in body_flvers[1:]:
                    for b in fv.get("Bones", []):
                        if b["Name"] not in mn and _keep(b):
                            merged.append(b)
                            mn.add(b["Name"])
        return merged

    @staticmethod
    def _resolve_mtd_for_materials(materials, mtd_root):
        from sekiro_asset_manager.mtd_parser import MtdParser
        enhanced = []
        for mat in materials:
            mp = mat.get("MTD", "")
            md = None
            if mp and mtd_root:
                mf = os.path.join(mtd_root, os.path.basename(mp.replace("\\", "/")))
                if os.path.exists(mf):
                    try:
                        md = MtdParser.parse(mf)
                    except Exception:
                        pass
            enhanced.append({
                "Name": mat.get("Name", ""),
                "MTD": mp,
                "Part": mat.get("Part", ""),
                "AvailableTextures": mat.get("AvailableTextures", []),
                "MTDInfo": md if md else {},
                "Textures": mat.get("Textures", []),
            })
        return enhanced

    @staticmethod
    def _collect_available_textures(tpf_roots, part_name):
        avail = []
        if not tpf_roots:
            return avail
        for root in tpf_roots:
            if not os.path.isdir(root):
                continue
            for f in os.listdir(root):
                fp = os.path.join(root, f)
                if os.path.isfile(fp) and f.lower().endswith((".dds", ".png", ".tga")):
                    avail.append(f)
            pd = os.path.join(root, part_name + "-tpf")
            if os.path.isdir(pd):
                for f in os.listdir(pd):
                    fp = os.path.join(pd, f)
                    if os.path.isfile(fp) and f.lower().endswith((".dds", ".png", ".tga")):
                        avail.append(f)
        return avail


    @staticmethod
    def _add_tx(tx, semantic, path):
        if semantic not in tx:
            tx[semantic] = path
        else:
            idx = 1
            while f"{semantic}:{idx}" in tx:
                idx += 1
            tx[f"{semantic}:{idx}"] = path

    @staticmethod
    def _has_tx_semantic(tx, semantic):
        return semantic in tx

    def _build_resolved_materials(materials_with_mtd, mtd_root, tpf_roots, texture_lookup_dirs=None):
        """Build ResolvedMaterials using MTD Path metadata for deterministic matching.
        
        Strategy:
        1. Use MTD Path filenames (the authoritative source from the game)
        2. Map filenames to semantic types by suffix (_a=albedo, _n=normal, etc.)
        3. Look up each filename in available textures (with .tif -> .png/.dds conversion)
        4. Only use textures whose filename stem matches an available texture
        """

        # Build set of available texture stems + full list for fallback
        available_stems = set()
        all_available = []
        lookup_dirs = texture_lookup_dirs if texture_lookup_dirs else (tpf_roots or [])
        if lookup_dirs:
            for root in lookup_dirs:
                if os.path.isdir(root):
                    for f in os.listdir(root):
                        if f.lower().endswith((".png", ".dds", ".tga")):
                            available_stems.add(os.path.splitext(f)[0].lower())
                            all_available.append(f)

        resolved = []
        for mat in materials_with_mtd:
            mi = mat.get("MTDInfo", {})
            tx = {}

            # Use MTD Path metadata (deterministic source of truth)
            mi_textures = mi.get("Textures", [])
            if isinstance(mi_textures, list):
                for tex_entry in mi_textures:
                    path = tex_entry.get("Path", "")
                    tex_type = tex_entry.get("Type", "")
                    if not path:
                        continue

                    # Extract filename and map to semantic
                    filename = os.path.basename(path)
                    stem = os.path.splitext(filename)[0].lower()

                    # Determine semantic from the type string or filename suffix
                    semantic = None
                    type_lower = tex_type.lower()
                    if "albedomap" in type_lower or "diffusemap" in type_lower:
                        semantic = "albedo"
                    elif "normalmap" in type_lower:
                        semantic = "normal"
                    elif "metallicmap" in type_lower or "specularmap" in type_lower:
                        semantic = "metallic"
                    elif "roughnessmap" in type_lower:
                        semantic = "roughness"
                    elif "emissivemap" in type_lower:
                        semantic = "emissive"
                    elif "aomap" in type_lower or "ambientocclusion" in type_lower or "occultusionmap" in type_lower:
                        semantic = "ao"
                    elif "opacitymap" in type_lower or "maskmap" in type_lower or "mask1map" in type_lower:
                        semantic = "opacityMask"
                    elif "reflectancemap" in type_lower:
                        semantic = "metallic"
                    
                    # Unknown MTD type: skip suffix fallback (suffix matching is unreliable
                    # when we have type information from MTD but just don't recognize it,
                    # e.g. DisplacementMap _d should NOT map to albedo)
                    if not semantic:
                        continue

                    # Look up the filename stem in available textures
                    if stem in available_stems:
                        # Find the actual file (prefer .png over .dds)
                        found = None
                        for ext in (".png", ".dds", ".tga"):
                            candidate = os.path.splitext(filename)[0] + ext
                            matched = False
                            for lroot in lookup_dirs:
                                if os.path.isdir(lroot) and candidate.lower() in [f.lower() for f in os.listdir(lroot)]:
                                    matched = True
                                    break
                            if matched:
                                found = candidate
                                break
                        if not found:
                            # Just use the stem to find any matching file
                            for root in lookup_dirs:
                                if os.path.isdir(root):
                                    for f in os.listdir(root):
                                        if os.path.splitext(f)[0].lower() == stem:
                                            found = f
                                            break
                                if found:
                                    break
                        
                        if found:
                            # Multiple matches for same semantic: prefer base over damage/skin variants
                            if not ModelJsonBuilder._has_tx_semantic(tx, semantic):
                                ModelJsonBuilder._add_tx(tx, semantic, found)
                            else:
                                existing = tx[semantic].lower()
                                candidate = found.lower()
                                replaced = False
                                # Prefer non-damage over damage
                                if ('damage' not in candidate and 'damage' in existing):
                                    # Save replaced damage variant as multi-entry
                                    idx = 1
                                    while f"{semantic}:{idx}" in tx: idx += 1
                                    tx[f"{semantic}:{idx}"] = tx[semantic]
                                    tx[semantic] = found
                                    replaced = True
                                # Prefer head over skin variants
                                elif ('head' in candidate and 'head' not in existing and 'skin' in existing):
                                    # Save replaced skin variant as multi-entry
                                    idx = 1
                                    while f"{semantic}:{idx}" in tx: idx += 1
                                    tx[f"{semantic}:{idx}"] = tx[semantic]
                                    tx[semantic] = found
                                    replaced = True
                                # Prefer first-slot (_0) textures
                                # _0 suffix preference disabled: Sekiro naming uses _0 as serial prefix, not variant
                                # Multi-entry: different texture for same semantic
                                if not replaced and candidate != existing:
                                    ModelJsonBuilder._add_tx(tx, semantic, found)


            # Detect material type from ShaderPath (SPX shader), not MTD filename
            # MTD filename keywords like "fur_SSS" are misleading (face skin, not fur)
            spx_name = os.path.basename(mi.get("ShaderPath", "")).lower()
            mtd_filename = os.path.basename(mat.get("MTD", "")).lower()
            mat_name = mat.get("Name", "").lower()

            # SSS/Skin shaders should never be forced to Masked
            is_skin = "sss" in spx_name or "skin" in spx_name or "head" in mat_name or "_ck" in mat_name

            # ShaderPath-based detection (authoritative for rendering behavior)
            is_fur = "fur" in spx_name and not is_skin and "cloth" not in spx_name
            is_hair = ("hair" in spx_name or "hair" in mat_name) and not is_skin
            is_cloth = ("cloth" in spx_name or "cloth" in mtd_filename) and not is_skin
            is_decal = "decal" in mtd_filename or "decal" in spx_name

            # MTD g_BlendMode is always Normal for fur/hair/cloth (masking done in SPX shader)
            # Override to UE-compatible blend mode based on keywords
            mtd_blend = mi.get("BlendMode", "Normal")
            clip_value = 0.0
            if is_fur or is_hair:
                resolved_blend = "Masked"
                if "hair" in spx_name or "hair" in mat_name:
                    clip_value = 0.33
                else:
                    clip_value = 0.25
            elif "_blend" in mtd_filename and not is_skin:
                resolved_blend = "Translucent"
            elif is_cloth:
                resolved_blend = "Masked"
                clip_value = 0.5
            elif is_decal:
                resolved_blend = "Masked"
                clip_value = 0.25
            else:
                _blend_map = {"Normal": "Opaque", "TexEdge": "Masked", "Blend": "Translucent"}
                resolved_blend = _blend_map.get(mtd_blend, "Opaque")

            is_two_sided = is_cloth or is_fur or is_hair or is_decal

            resolved.append({
                "Name": mat.get("Name", ""),
                "Part": mat.get("Part", ""),
                "ShaderType": ModelJsonBuilder.classify_shader(mi.get("ShaderPath", "")),
                "ShaderPath": os.path.basename(mi.get("ShaderPath", "")),
                "ResolvedBlendMode": resolved_blend,
                "TwoSided": is_two_sided,
                "ClipValue": clip_value,
                "IsCloth": is_cloth,
                "IsHair": is_hair,
                "IsFur": is_fur,
                "IsDecal": is_decal,
                "DrawStep": "Masked" if (is_fur or is_hair) else mi.get("DrawStep", "Opaque"),
                "Textures": tx,
            })
        return resolved

class ModelImporter:
    """Coordinate full model import pipeline."""

    def __init__(self, config=None):
        from sekiro_asset_manager.pipeline_config import PipelineConfig, config as dc
        self.config = config or dc

    def _dn(self, asset_name, suffix, sub=None):
        d = os.path.join(self.config.project_dir, "Output", asset_name)
        os.makedirs(d, exist_ok=True)
        return os.path.join(d, asset_name + suffix)

    def _texture_dir(self, original_asset_name):
        d = os.path.join(self.config.project_dir, "Output", "Textures", original_asset_name)
        os.makedirs(d, exist_ok=True)
        return d

    def _shared_texture_dir(self):
        d = os.path.join(self.config.project_dir, "Output", "Textures", "Shared")
        os.makedirs(d, exist_ok=True)
        return d
        return os.path.join(d, asset_name + suffix)

    def import_model(self, asset_name, **kwargs):
        result = {"success": False, "asset_name": asset_name, "steps": {}}

        skeleton_flver = kwargs.get("skeleton_flver",
            os.path.join(self.config.extracted_dir, "chr", asset_name + ".flver"))
        skeleton_hkx = kwargs.get("skeleton_hkx",
            os.path.join(self.config.extracted_dir, "chr", asset_name + ".hkx"))
        skeleton_source = kwargs.get("skeleton_source")
        if not skeleton_source:
            if os.path.exists(skeleton_hkx):
                skeleton_source = skeleton_hkx
            else:
                aj = os.path.join(self.config.output_dir, asset_name + "_common_anims.json")
                if os.path.exists(aj):
                    skeleton_source = aj

        flver_paths = kwargs.get("flver_paths", [])
        output_json = kwargs.get("output_json", self._dn(asset_name, "_model.json"))
        output_fbx = kwargs.get("output_fbx", self._dn(asset_name, ".fbx"))
        texture_root = kwargs.get("texture_root", self.config.extracted_dir)
        target_ue_path = kwargs.get("target_ue_path",
            self.config.ue_content_root + "/" + asset_name)
        skip_json_generation = kwargs.get("skip_json_generation", False)
        model_json = kwargs.get("model_json")

        result["output_json"] = output_json
        result["output_fbx"] = output_fbx
        result["target_ue_path"] = target_ue_path

        if not flver_paths:
            cd = os.path.join(self.config.extracted_dir, "chr")
            if os.path.isdir(cd):
                for f in sorted(os.listdir(cd)):
                    if f.startswith(asset_name) and f.endswith(".flver") and "skeleton" not in f.lower():
                        flver_paths.append(os.path.join(cd, f))
        if not flver_paths:
            result["error"] = "No FLVER files for " + asset_name
            return result
        result["steps"]["resolve_flver"] = True

        if not skip_json_generation:
            try:
                ok = self.generate_model_json(
                    flver_paths=flver_paths, output_json=output_json,
                    skeleton_flver=skeleton_flver, skeleton_hkx=skeleton_hkx,
                    skeleton_source=skeleton_source,
                    drop_mesh_bone_prefixes=kwargs.get("drop_mesh_bone_prefixes"),
                    original_asset_name=kwargs.get("original_asset_name"),
                    asset_name=asset_name,
                    auxiliary_bones=kwargs.get("auxiliary_bones"))
                if not ok:
                    result["error"] = "JSON generation failed"
                    return result
                result["steps"]["json_generation"] = True
            except Exception as e:
                result["error"] = "JSON gen: " + str(e)
                return result

        try:
            anim_json = skeleton_source if (skeleton_source and skeleton_source.endswith(".json")) else self._dn(asset_name, "_common_anims.json", "")
            fbx_ok = self._run_blender_fbx_export(
                model_json=model_json or output_json,
                anim_json=anim_json,
                output_fbx=output_fbx,
                texture_root=texture_root)
            result["steps"]["blender_fbx"] = fbx_ok
            if not fbx_ok:
                result["error"] = "Blender FBX export failed"
                return result
        except Exception as e:
            result["error"] = "Blender: " + str(e)
            return result

        try:
            ue_ok = self._run_ue_import(
                fbx_path=output_fbx,
                target_path=target_ue_path,
                model_json=model_json or output_json,
                skeleton_name=kwargs.get("skeleton_name", self._infer_skeleton_name(model_json or output_json)))
            result["steps"]["ue_import"] = ue_ok
            if not ue_ok:
                result["error"] = "UE import failed"
                return result
        except Exception as e:
            result["error"] = "UE: " + str(e)
            return result

        result["success"] = True
        return result

    def generate_model_json(self, flver_paths, output_json, skeleton_flver=None,
                         skeleton_hkx=None, skeleton_source=None,
                         drop_mesh_bone_prefixes=None, original_asset_name=None,
                         asset_name=None, auxiliary_bones=None):
        for fp in flver_paths:
            if not os.path.exists(fp):
                print(f"  [Error] FLVER not found: {fp}")
                return False
        tpf_roots = self._find_tpf_roots(flver_paths)
        mtd_root = os.path.join(self.config.extracted_dir, "mtd")
        asset_name = asset_name or os.path.splitext(os.path.basename(output_json))[0]
        orig_name = original_asset_name or asset_name
        skeleton_source = skeleton_source or skeleton_hkx
        # Include shared extracted textures
        shared_extracted = os.path.join(self.config.extracted_dir, "Textures")
        if os.path.isdir(shared_extracted) and shared_extracted not in tpf_roots:
            tpf_roots.append(shared_extracted)
        # Convert DDS -> PNG to Output/Textures/{orig_name}/
        tex_out = self._texture_dir(orig_name)
        self._convert_tpf_dds_to_png(tpf_roots, tex_out)
        # Copy shared PNGs to Output/Textures/Shared/
        import shutil
        shared_tex = self._shared_texture_dir()
        for f in os.listdir(shared_extracted):
            if f.lower().endswith('.png'):
                src = os.path.join(shared_extracted, f)
                dst = os.path.join(shared_tex, f)
                if not os.path.exists(dst):
                    shutil.copy2(src, dst)

        try:
            tex_output_dir = self._texture_dir(orig_name)
            model_data = ModelJsonBuilder.build(
                flver_paths=flver_paths,
                skeleton_flver=skeleton_flver,
                skeleton_source=skeleton_source,
                mtd_root=mtd_root,
                tpf_roots=tpf_roots,
                asset_name=asset_name,
                drop_mesh_bone_prefixes=drop_mesh_bone_prefixes,
                texture_lookup_dirs_extra=[tex_output_dir, self._shared_texture_dir()],
                original_asset_name=orig_name,
                auxiliary_bones=auxiliary_bones)
        except Exception as e:
            print(f"  [Error] Model JSON build failed: {e}")
            import traceback
            traceback.print_exc()
            return False

        os.makedirs(os.path.dirname(output_json), exist_ok=True)
        with open(output_json, "w", encoding="utf-8") as f:
            json.dump(model_data, f, indent=2, ensure_ascii=False)

        bones = len(model_data.get("Bones", []))
        auxiliary_bones_count = len(model_data.get("AuxiliaryBones", []))
        meshes = len(model_data.get("Meshes", []))
        mats = len(model_data.get("Materials", []))
        print(
            f"  JSON generated: {bones} bones, {auxiliary_bones_count} auxiliary bones, "
            f"{meshes} meshes, {mats} materials -> {output_json}"
        )
        return True

    def _convert_tpf_dds_to_png(self, tpf_roots, output_dir=None):
        tc = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ext_tools", "texconv.exe")
        if not os.path.exists(tc):
            tc = os.path.join(_PROJECT_DIR, "Tools", "texconv.exe")
        if not os.path.exists(tc):
            return
        for root in tpf_roots:
            if not os.path.isdir(root):
                continue
            out = output_dir if output_dir else root
            os.makedirs(out, exist_ok=True)
            for f in os.listdir(root):
                if f.lower().endswith(".dds"):
                    dds = os.path.join(root, f)
                    png = os.path.join(out, os.path.splitext(f)[0] + ".png")
                    if not os.path.exists(png):
                        try:
                            subprocess.run([tc, "-ft", "png", "-y", "-o", out, dds],
                                           capture_output=True, timeout=30)
                        except Exception:
                            pass

    def _find_tpf_roots(self, flver_paths):
        roots = []
        for fp in flver_paths:
            pd = os.path.dirname(fp)
            pn = os.path.splitext(os.path.basename(fp))[0]
            for c in [os.path.join(pd, pn + "-tpf"), os.path.join(pd, "..", pn + "-tpf")]:
                c = os.path.normpath(c)
                if os.path.isdir(c) and c not in roots:
                    roots.append(c)
            pp = os.path.dirname(pd)
            if os.path.isdir(pp) and pp not in roots:
                roots.append(pp)
        return roots

    def fix_json(self, json_path, anim_json_path=None):
        if not os.path.exists(json_path):
            return False
        with open(json_path, "r", encoding="utf-8") as f:
            d = json.load(f)
        if "SkeletonName" not in d:
            d["SkeletonName"] = "Sekiro"
        bn = {b["Name"]: b for b in d.get("Bones", [])}
        for b in d.get("Bones", []):
            pn = b.get("ParentName", "")
            if pn and pn in bn:
                b["ParentIndex"] = list(bn.keys()).index(pn)
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(d, f, indent=2, ensure_ascii=False)
        return True

    def _infer_skeleton_name(self, model_json):
        if model_json and os.path.exists(model_json):
            try:
                with open(model_json, "r", encoding="utf-8") as f:
                    return json.load(f).get("SkeletonName", "Sekiro")
            except Exception:
                pass
        return "Sekiro"

    def _run_blender_fbx_export(self, model_json, anim_json, output_fbx, texture_root):
        be = self.config.tool_path("blender")
        if not be:
            for p in ["blender", "C:/Program Files/Blender Foundation/Blender 4.0/blender.exe"]:
                if shutil.which(p) or os.path.exists(p):
                    be = p
                    break
        if not be:
            print("  [Error] Blender not found")
            return False
        imp = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "import_model.py"))
        if not os.path.exists(imp):
            print(f"  [Error] import_model.py not found: {imp}")
            return False
        cmd = [be, "--background", "--python", imp, "--", model_json, anim_json, output_fbx, texture_root]
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
            if r.returncode != 0:
                print(f"  [Error] Blender ({r.returncode}): {r.stderr[-200:]}")
                return False
            print("  Blender FBX export done:", output_fbx)
            return True
        except subprocess.TimeoutExpired:
            print("  [Error] Blender timeout")
            return False

    def _run_ue_import(self, fbx_path, target_path, model_json=None, skeleton_name="Sekiro"):
        ue = self.config.tool_path("ue_editor")
        if not ue or not os.path.exists(ue):
            print("  [Error] UE Editor not found")
            return False
        cmd = [ue, "-run=SekiroImport.ImportModel",
               "-fbx=" + fbx_path, "-target=" + target_path, "-skeleton=" + skeleton_name]
        if model_json:
            cmd.append("-json=" + model_json)
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
            if r.returncode != 0:
                print(f"  [Error] UE import ({r.returncode}): {r.stderr[-200:]}")
                return False
            print("  UE import done:", target_path)
            return True
        except subprocess.TimeoutExpired:
            print("  [Error] UE import timeout")
            return False









