import json

# Check the model JSON to see real MTD values
with open('D:/Sekiro/Extracted/Sekiro_model_hkx.json') as f:
    data = json.load(f)

# Gather unique MTDs per material
seen = {}
for mesh in data.get('Meshes', []):
    mat_idx = mesh.get('MaterialIndex', 0)
    mats = data.get('Materials', [])
    if mat_idx < len(mats):
        mat = mats[mat_idx]
        name = mat.get('Name', '?')
        mtd = mat.get('MTD', '').replace('\\', '/').split('/')[-1]
        part = mesh.get('Part', '?')
        key = f"{part}_{name}"
        if key not in seen:
            seen[key] = mtd

print(f"Unique material/part combos: {len(seen)}")
print()
for key, mtd in sorted(seen.items()):
    mtd_l = mtd.lower()
    cloth = 'cloth' in mtd_l
    decal = 'decal' in mtd_l
    hair  = 'hair' in mtd_l
    if cloth or decal or hair:
        tag = f"[cloth={cloth} decal={decal} hair={hair}]"
    else:
        tag = "[Opaque]"
    print(f"  {key}: {mtd} {tag}")
