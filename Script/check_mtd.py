import json

with open('D:/Sekiro/Extracted/Textures/Sekiro_Materials.json') as f:
    data = json.load(f)

materials = data['materials']
print('Total:', len(materials))
print()

for m in materials:
    raw_mtd = m.get('mtd', '')
    mtd = raw_mtd.replace('\\', '/').split('/')[-1].lower()
    blend = m['blend_mode']
    has_cloth = 'cloth' in mtd
    has_decal = 'decal' in mtd
    has_hair = 'hair' in mtd
    flag = ''
    if has_cloth or has_hair or has_decal:
        flag = f' [cloth={has_cloth} decal={has_decal} hair={has_hair}]'
    print(f"{m['name']}: {mtd} -> {blend}{flag}")
