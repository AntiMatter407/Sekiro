import json
with open(r"F:\ProjectAI\Sekiro\Extracted\c0000_model.json", "r", encoding="utf-8") as f:
    data = json.load(f)

# Check all AM materials' resolved textures
am_mats = [m for m in data.get("ResolvedMaterials", []) if m["Part"] == "AM_M_9000"]
print(f"AM materials: {len(am_mats)}")
for m in am_mats:
    tx = m.get("Textures", {})
    print(f"  {m['Name']}: {tx}")