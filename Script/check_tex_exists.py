import json, os

with open('D:/Sekiro/Extracted/Textures/Sekiro_Materials.json') as f:
    data = json.load(f)

tex_dir = 'D:/Sekiro/Extracted/Textures'

print("=== Translucent / Masked materials and their textures ===\n")
for m in data['materials']:
    blend = m['blend_mode']
    if blend not in ('Translucent', 'Masked'):
        continue
    name = m['name']
    textures = m.get('textures', {})
    print(f"[{blend}] {name}")
    if not textures:
        print("  !! NO TEXTURES")
    for slot, fname in textures.items():
        path = os.path.join(tex_dir, fname)
        exists = os.path.exists(path)
        print(f"  {slot}: {fname}  {'OK' if exists else 'MISSING!'}")
    print()
