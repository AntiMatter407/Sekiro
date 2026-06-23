import re

with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "r", encoding="utf-8") as f:
    content = f.read()

# Find the function boundaries
pattern = r'(    def _match_textures_by_name\(mat_name, part_name, available_files\):.*?    @staticmethod)'
match = re.search(pattern, content, re.DOTALL)
if not match:
    print("Could not find function")
    exit(1)

old_func = match.group(1)[:-len('    @staticmethod')]  # remove the trailing @staticmethod
print(f"Old function length: {len(old_func)} chars")

new_func = '''    def _match_textures_by_name(mat_name, part_name, available_files):
        """Match material to closest texture stem by name similarity.
        
        Uses a two-level strategy:
        1. If part_name is known, match sub-name (the part after the common prefix)
        2. Otherwise, compare full material name against texture stems
        Only assigns textures when score >= 200 (meaningful match).
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
        tex_map = {}
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
        best_stem = None
        best_score = -1

        # Extract the material sub-name (part after the common prefix)
        if part_lower and mat_name_lower.startswith(part_lower):
            sub_name = mat_name_lower[len(part_lower):].lstrip("_")
        else:
            sub_name = mat_name_lower.replace("_", "")

        for stem in tex_map:
            score = 0
            if part_lower and stem.startswith(part_lower):
                # Same part prefix: compare sub-names
                stem_sub = stem[len(part_lower):].lstrip("_")
                stem_sub_clean = stem_sub.replace("_", "")
                sub_name_clean = sub_name.replace("_", "")
                
                if sub_name_clean == stem_sub_clean:
                    score = 1000  # exact sub-name match
                elif sub_name_clean and stem_sub_clean and (
                    sub_name_clean in stem_sub_clean or stem_sub_clean in sub_name_clean):
                    score = 700  # substring match within same part
                else:
                    # Weak: same part but different sub-name
                    # Only accept if sub-names share significant characters
                    common = sum(1 for a, b in zip(sub_name_clean, stem_sub_clean) if a == b)
                    score = 50 + common
            else:
                # Different part or no part: compare full stems
                mat_clean = mat_name_lower.replace("_", "")
                stem_clean = stem.replace("_", "")
                if stem_clean == mat_clean:
                    score = 900
                elif mat_clean in stem_clean or stem_clean in mat_clean:
                    score = 600
                else:
                    common = sum(1 for a, b in zip(stem_clean, mat_clean) if a == b)
                    score = max(0, common - 5)

            if score > best_score:
                best_score = score
                best_stem = stem

        # Only assign textures if we have a meaningful match (score >= 200)
        MIN_SCORE = 200
        tx = {}
        if best_stem and best_score >= MIN_SCORE and best_stem in tex_map:
            for f, semantic in tex_map[best_stem]:
                if semantic not in tx:
                    tx[semantic] = f
        return tx
'''

# Replace old function with new
content = content.replace(old_func, new_func)

with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "w", encoding="utf-8") as f:
    f.write(content)

print("Replaced _match_textures_by_name")