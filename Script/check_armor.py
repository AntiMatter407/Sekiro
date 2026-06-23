import json
with open(r"F:\ProjectAI\Sekiro\Extracted\c0000_model.json", "r", encoding="utf-8") as f:
    data = json.load(f)

# Check which materials get Armor textures
armor_count = 0
for m in data.get("ResolvedMaterials", []):
    tx = m.get("Textures", {})
    vals = list(tx.values())
    if any("Armor" in v for v in vals):
        armor_count += 1
        print(f"{m['Name']} ({m['Part']}): {vals}")

print(f"\nTotal with Armor: {armor_count}")
print(f"Total materials: {len(data.get('ResolvedMaterials', []))}")