import json
import os

with open(r"F:\ProjectAI\Sekiro\Extracted\c0000_model.json", "r", encoding="utf-8") as f:
    data = json.load(f)

# Check MTDInfo texture Paths for each material
for m in data["Materials"]:
    mi = m.get("MTDInfo", {})
    paths = []
    for t in mi.get("Textures", []):
        p = t.get("Path", "")
        if p:
            paths.append(p)
    if paths:
        print(f"{m['Name']} ({m['Part']}):")
        for p in paths:
            fn = os.path.basename(p)
            print(f"  {fn}")
        print()