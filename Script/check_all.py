import json
with open(r"F:\ProjectAI\Sekiro\Extracted\c0000_model.json", "r", encoding="utf-8") as f:
    data = json.load(f)

from collections import Counter

# Check per-part distribution
part_counts = Counter()
for m in data["Materials"]:
    part_counts[m.get("Part", "?")] += 1
print("Materials per part:")
for part, count in part_counts.most_common():
    print(f"  {part}: {count}")

print()

# Check which materials have textures
empty = []
for m in data["Materials"]:
    tx = m.get("Textures", {})
    if not tx:
        empty.append(f"{m['Name']} ({m['Part']})")
if empty:
    print(f"Materials without textures ({len(empty)}):")
    for e in empty:
        print(f"  {e}")

# Check LG specifically
print()
print("LG materials:")
for m in data["Materials"]:
    if m.get("Part") == "LG_M_9000":
        tx = m.get("Textures", {})
        print(f"  {m['Name']}: {tx}")

print()
print("HD materials:")
for m in data["Materials"]:
    if m.get("Part") in ("HD_M_9510", "HD_M_9520"):
        tx = m.get("Textures", {})
        print(f"  {m['Name']} ({m['Part']}): {tx}")