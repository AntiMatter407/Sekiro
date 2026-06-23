import unreal

base = "/Game/Sekiro/Character/c0000"
eal = unreal.EditorAssetLibrary

# Create Materials folder
if not eal.does_directory_exist(base + "/Materials"):
    eal.make_directory(base + "/Materials")

# Move all M_ assets to Materials/
assets = eal.list_assets(base, recursive=False)
moved = 0
for path in assets:
    name = path.split("/")[-1]
    if name.startswith("M_"):
        new_path = base + "/Materials/" + name
        if not eal.does_asset_exist(new_path):
            eal.rename_asset(path, new_path)
            moved += 1
print(f"Moved {moved} materials")

# Update mesh material references
mesh = eal.load_asset(base + ".c0000")
if mesh:
    mats = mesh.get_editor_property("materials")
    updated = 0
    for i, m in enumerate(mats):
        if m.material_interface:
            old_path = m.material_interface.get_path_name()
            old_name = old_path.split("/")[-1]
            new_path = base + "/Materials/" + old_name
            new_mat = eal.load_asset(new_path)
            if new_mat:
                mats[i].material_interface = new_mat
                updated += 1
    mesh.set_editor_property("materials", mats)
    eal.save_asset(base + ".c0000")
    print(f"Updated {updated} material slots on mesh")
