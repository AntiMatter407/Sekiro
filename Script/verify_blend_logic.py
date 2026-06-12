import json, os

# Simulate the fixed logic against real MTD data
with open('D:/Sekiro/Extracted/Sekiro_model_hkx.json') as f:
    data = json.load(f)

mats = data.get('Materials', [])
print(f"Total materials in model JSON: {len(mats)}")
print()

summary = {'Opaque': 0, 'Masked': 0, 'Translucent': 0}
for mat in mats:
    name = mat.get('Name', '?')
    raw_mtd = mat.get('MTD', '')
    mtd = os.path.basename(raw_mtd.replace('\\', '/')).lower()

    is_decal = 'decal' in mtd
    is_cloth_decal = is_decal and 'cloth' in mtd
    is_cloth = 'cloth' in mtd if mtd else False

    if is_cloth_decal or is_cloth:
        blend = 'Masked'
    elif is_decal:
        blend = 'Translucent'
    else:
        blend = 'Opaque'

    summary[blend] = summary.get(blend, 0) + 1
    two_sided = is_decal or is_cloth
    print(f"  [{blend:12s}] {name}: {mtd} (two_sided={two_sided})")

print()
print("=== Summary ===")
for k, v in summary.items():
    print(f"  {k}: {v}")
