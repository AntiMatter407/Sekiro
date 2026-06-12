import json, os

with open(r'D:\Sekiro\Extracted\Textures\Sekiro_Materials.json') as f:
    data = json.load(f)

materials = data['materials']
print(f"Total materials: {len(materials)}\n")

issues = []
summary = {'Opaque': 0, 'Masked': 0, 'Translucent': 0}

for m in materials:
    name     = m['name']
    blend    = m['blend_mode']
    two_sided= m['two_sided']
    textures = m.get('textures', {})
    mtd_full = m.get('mtd', '').replace('\\', '/')
    mtd      = mtd_full.split('/')[-1].lower()

    summary[blend] = summary.get(blend, 0) + 1
    problems = []

    # Decal (non-cloth) should be Translucent
    if 'decal' in mtd and 'cloth' not in mtd and blend != 'Translucent':
        problems.append(f'Decal MTD but blend={blend}, expected Translucent')

    # Cloth+Decal should be Masked
    if 'decal' in mtd and 'cloth' in mtd and blend != 'Masked':
        problems.append(f'Cloth+Decal MTD but blend={blend}, expected Masked')

    # Translucent/Masked must have _a
    if blend in ('Translucent', 'Masked') and '_a' not in textures:
        problems.append(f'{blend} but no _a texture')

    # No textures at all
    if not textures:
        problems.append('NO TEXTURES')

    # Missing normal map (warn only if has albedo)
    if '_a' in textures and '_n' not in textures:
        problems.append('missing _n (normal)')

    # Translucent: confirm _a is present for Alpha->Opacity
    if blend == 'Translucent' and '_a' in textures:
        pass  # OK

    if problems:
        issues.append((name, blend, two_sided, mtd, list(textures.keys()), problems))

print("=== Blend Mode Summary ===")
for k, v in summary.items():
    print(f"  {k}: {v}")
print()

print(f"=== Issues ({len(issues)}) ===")
for name, blend, two_sided, mtd, tex_keys, problems in issues:
    print(f"[{name}]")
    print(f"  blend={blend}  two_sided={two_sided}")
    print(f"  mtd={mtd}")
    print(f"  textures={tex_keys}")
    for p in problems:
        print(f"  !! {p}")
    print()

print("=== All Translucent materials ===")
for m in materials:
    if m['blend_mode'] == 'Translucent':
        textures = m.get('textures', {})
        mtd = m.get('mtd', '').replace('\\', '/').split('/')[-1]
        print(f"  {m['name']}")
        print(f"    mtd={mtd}")
        print(f"    textures={list(textures.keys())}")
