import re

with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "r", encoding="utf-8") as f:
    content = f.read()

# Replace the entire _build_resolved_materials function
old_pattern = r'    def _build_resolved_materials\(materials_with_mtd, mtd_root, tpf_roots\):.*?    @staticmethod'
match = re.search(old_pattern, content, re.DOTALL)
if not match:
    # Try without @staticmethod at end
    old_pattern = r'    def _build_resolved_materials\(materials_with_mtd, mtd_root, tpf_roots\):.*?(?=\n    @staticmethod|\nclass |\Z)'
    match = re.search(old_pattern, content, re.DOTALL)

if not match:
    print("Could not find _build_resolved_materials")
    exit(1)

old_func = match.group()

new_func = '''    def _build_resolved_materials(materials_with_mtd, mtd_root, tpf_roots):
        """Build ResolvedMaterials using MTD Path metadata for deterministic matching.
        
        Strategy:
        1. Use MTD Path filenames (the authoritative source from the game)
        2. Map filenames to semantic types by suffix (_a=albedo, _n=normal, etc.)
        3. Look up each filename in available textures (with .tif -> .png/.dds conversion)
        4. Only use textures whose filename stem matches an available texture
        """
        SEMANTIC_FROM_SUFFIX = {
            "_a": "albedo", "_d": "albedo",
            "_n": "normal",
            "_m": "metallic", "_s": "metallic",
            "_r": "roughness",
            "_em": "emissive",
            "_ao": "ao",
            "_mask": "opacityMask",
        }

        # Build set of available texture stems
        available_stems = set()
        if tpf_roots:
            for root in tpf_roots:
                if os.path.isdir(root):
                    for f in os.listdir(root):
                        if f.lower().endswith((".png", ".dds", ".tga")):
                            available_stems.add(os.path.splitext(f)[0].lower())

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
                    elif "aomap" in type_lower or "ambientocclusion" in type_lower:
                        semantic = "ao"
                    elif "opacitymap" in type_lower or "maskmap" in type_lower:
                        semantic = "opacityMask"
                    
                    # Fallback: determine from filename suffix
                    if not semantic:
                        for suffix, sem in sorted(SEMANTIC_FROM_SUFFIX.items(), key=lambda x: -len(x[0])):
                            if stem.endswith(suffix):
                                semantic = sem
                                break

                    if not semantic:
                        continue

                    # Look up the filename stem in available textures
                    if stem in available_stems:
                        # Find the actual file (prefer .png over .dds)
                        found = None
                        for ext in (".png", ".dds", ".tga"):
                            candidate = os.path.splitext(filename)[0] + ext
                            if candidate.lower() in [f.lower() for f in os.listdir(tpf_roots[0]) if os.path.isdir(tpf_roots[0])] + (
                                [f.lower() for f in os.listdir(tpf_roots[1])] if len(tpf_roots) > 1 and os.path.isdir(tpf_roots[1]) else []):
                                found = candidate
                                break
                        if not found:
                            # Just use the stem to find any matching file
                            for root in tpf_roots:
                                if os.path.isdir(root):
                                    for f in os.listdir(root):
                                        if os.path.splitext(f)[0].lower() == stem:
                                            found = f
                                            break
                                if found:
                                    break
                        
                        if found and semantic not in tx:
                            tx[semantic] = found

            resolved.append({
                "Name": mat.get("Name", ""),
                "Part": mat.get("Part", ""),
                "ResolvedBlendMode": mi.get("BlendMode", "Opaque"),
                "TwoSided": mi.get("TwoSided", False),
                "IsCloth": mi.get("IsCloth", False),
                "IsHair": mi.get("IsHair", False),
                "IsFur": mi.get("IsFur", False),
                "IsDecal": mi.get("IsDecal", False),
                "DrawStep": mi.get("DrawStep", "Opaque"),
                "Textures": tx,
            })
        return resolved
'''

content = content.replace(old_func, new_func)
print("Replaced" if new_func in content else "Failed")

with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "w", encoding="utf-8") as f:
    f.write(content)