import re

with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "r", encoding="utf-8") as f:
    content = f.read()

# Find the function and replace it completely
pattern = r'    def _match_textures_by_name\(mat_name, part_name, available_files\):.*?    @staticmethod'
match = re.search(pattern, content, re.DOTALL)
if not match:
    print("Could not find function")
    exit(1)

old_func = match.group()[:-len('    @staticmethod')]

new_func = '''    def _match_textures_by_name(mat_name, part_name, available_files):
        """Deterministic texture matching by FLVER/TPF association.
        
        Rule: Materials from FLVER X can ONLY use textures from TPF X
        (i.e., textures with the same part prefix).
        
        1. Filter textures to only those matching the part prefix
        2. Try exact sub-name match first
        3. Try substring sub-name match
        4. Fall back to the part's base texture set (shortest sub-name)
        """
        SEMANTIC_SUFFIXES = {
            "_a": "albedo", "_d": "albedo",
            "_n": "normal",
            "_m": "metallic", "_s": "metallic",
            "_r": "roughness",
            "_em": "emissive",
            "_ao": "ao",
            "_mask": "opacityMask",
        }

        # Build texture map, grouped by stem
        tex_map = {}  # stem -> [(filename, semantic), ...]
        for f in available_files:
            fn = os.path.splitext(f)[0]
            fn_lower = fn.lower()
            for suffix, semantic in sorted(SEMANTIC_SUFFIXES.items(), key=lambda x: -len(x[0])):
                if fn_lower.endswith(suffix):
                    stem = fn[:-len(suffix)].lower()
                    tex_map.setdefault(stem, []).append((f, semantic))
                    break

        mat_name_lower = mat_name.lower()
        part_lower = part_name.lower() if part_name else ""

        if not part_lower:
            return {}

        # Extract material sub-name (part after the common prefix)
        if mat_name_lower.startswith(part_lower):
            sub_name = mat_name_lower[len(part_lower):].lstrip("_")
        else:
            sub_name = mat_name_lower

        # Step 1: Filter to ONLY textures from the same part (FLVER/TPF association)
        same_part_stems = {}
        for stem, entries in tex_map.items():
            if stem.startswith(part_lower):
                same_part_stems[stem] = entries

        if not same_part_stems:
            return {}  # No textures for this part

        # Step 2: Try to match by sub-name
        sub_name_clean = sub_name.replace("_", "")
        best_stem = None
        best_score = -1

        for stem in same_part_stems:
            stem_sub = stem[len(part_lower):].lstrip("_")
            stem_sub_clean = stem_sub.replace("_", "")
            
            score = 0
            if sub_name_clean and stem_sub_clean:
                if sub_name_clean == stem_sub_clean:
                    score = 1000  # exact match
                elif sub_name_clean in stem_sub_clean or stem_sub_clean in sub_name_clean:
                    score = 700  # substring match
            
            # Tiebreaker: prefer shorter stem_sub (more generic)
            if score > best_score or (score == best_score and best_stem and len(stem_sub) < len(best_stem)):
                best_score = score
                best_stem = stem

        # Step 3: If no meaningful match (score < 700), use part's base texture set
        # Base = the texture set with the shortest sub-name (most generic)
        if best_score < 700:
            best_stem = None
            for stem in same_part_stems:
                stem_sub = stem[len(part_lower):].lstrip("_")
                if best_stem is None or len(stem_sub) < len(best_stem):
                    best_stem = stem
                elif len(stem_sub) == len(best_stem):
                    # Same length: prefer the one that's alphabetically first
                    if stem_sub < best_stem:
                        best_stem = stem

        # Build result
        tx = {}
        if best_stem and best_stem in same_part_stems:
            for f, semantic in same_part_stems[best_stem]:
                if semantic not in tx:
                    tx[semantic] = f
        return tx
'''

content = content.replace(old_func, new_func)
print("Replaced function" if new_func in content else "Failed")

with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "w", encoding="utf-8") as f:
    f.write(content)