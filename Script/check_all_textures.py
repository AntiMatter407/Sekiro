import json, os

with open('D:/Sekiro/Extracted/Textures/Sekiro_Materials.json') as f:
    data = json.load(f)

tex_dir = 'D:/Sekiro/Extracted/Textures'

print("=== All materials - texture presence check ===\n")
issues = []
for m in data['materials']:
    name = m['name']
    textures = m.get('textures', {})
    missing = []
    for slot, fname in textures.items():
        path = os.path.join(tex_dir, fname)
        if not os.path.exists(path):
            missing.append(f"{slot}: {fname}")
    if not textures:
        issues.append((name, m['blend_mode'], ['NO TEXTURES']))
    elif missing:
        issues.append((name, m['blend_mode'], missing))

print(f"Materials with issues: {len(issues)}")
for name, blend, probs in issues:
    print(f"  [{blend}] {name}")
    for p in probs:
        print(f"    !! {p}")
