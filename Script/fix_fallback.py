import re

with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "r", encoding="utf-8") as f:
    content = f.read()

# Find the end of the MTD-based texture lookup in _build_resolved_materials
# Add fallback: if no textures found via MTD Path, use name matching
old = """            resolved.append({
                "Name": mat.get("Name", ""),
                "Part": mat.get("Part", ""),"""

# Replace to add fallback
new = """            # Fallback: if no textures found via MTD Path, try name-based matching
            if not tx:
                tx = ModelJsonBuilder._match_textures_by_name(
                    mat.get("Name", ""), mat.get("Part", ""), all_available
                )

            resolved.append({
                "Name": mat.get("Name", ""),
                "Part": mat.get("Part", ""),"""

content = content.replace(old, new)
print("Added fallback" if new in content else "Failed")

with open(r"F:\ProjectAI\Sekiro\Script\sekiro_asset_manager\model_importer.py", "w", encoding="utf-8") as f:
    f.write(content)