import sys, os, json
sys.path.insert(0, r"F:\ProjectAI\Sekiro\Script")
from sekiro_asset_manager.model_importer import ModelJsonBuilder

with open(r"F:\ProjectAI\Sekiro\Extracted\c0000_model.json", "r", encoding="utf-8") as f:
    data = json.load(f)

materials_with_mtd = []
for m in data.get("Materials", []):
    materials_with_mtd.append({
        "Name": m.get("Name", ""),
        "Part": m.get("Part", ""),
        "MTDInfo": m.get("MTDInfo", {}),
    })

tpf_roots = [r"F:\ProjectAI\Sekiro\Extracted\Textures"]
sub = os.path.join(tpf_roots[0], "Textures")
if os.path.isdir(sub):
    tpf_roots.append(sub)

resolved = ModelJsonBuilder._build_resolved_materials(materials_with_mtd, None, tpf_roots)

# Build lookup from resolved
resolved_by_key = {}
for r in resolved:
    key = (r.get("Part", ""), r.get("Name", ""))
    resolved_by_key[key] = r

# Sync textures into Materials array (C++ reads Materials[].Textures first)
for m in data["Materials"]:
    old_name = m.get("Name", "")
    part = m.get("Part", "")
    
    # Find matching resolved material
    matched = None
    for key in [(part, old_name), ("", old_name)]:
        if key in resolved_by_key:
            matched = resolved_by_key[key]
            break
    
    if matched:
        m["Textures"] = matched.get("Textures", {})
        for k in ("ResolvedBlendMode", "TwoSided", "IsCloth", "IsHair", "IsFur", "IsDecal", "DrawStep"):
            m[k] = matched.get(k, m.get(k))

data["ResolvedMaterials"] = resolved

# Verify sync
armor_in_materials = sum(1 for m in data["Materials"] if "Armor" in str(m.get("Textures", {})))
armor_in_resolved = sum(1 for r in resolved if "Armor" in str(r.get("Textures", {})))
print(f"Armor refs in Materials: {armor_in_materials}")
print(f"Armor refs in ResolvedMaterials: {armor_in_resolved}")

with open(r"F:\ProjectAI\Sekiro\Extracted\c0000_model.json", "w", encoding="utf-8") as f:
    json.dump(data, f, indent=2, ensure_ascii=False)
print("JSON synced and saved!")