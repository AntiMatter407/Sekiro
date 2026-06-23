import unreal

base = "/Game/Sekiro/Character/c0000"
EditorAssetLibrary = unreal.EditorAssetLibrary

# List all assets in base
assets = EditorAssetLibrary.list_assets(base, recursive=False)
moved = 0
for path in assets:
    name = path.split("/")[-1]
    if name.startswith("M_"):
        new_path = base + "/Materials/" + name
        if not EditorAssetLibrary.does_asset_exist(new_path):
            EditorAssetLibrary.rename_asset(path, new_path)
            moved += 1
            print(f"Moved: {name} -> Materials/")

print(f"Done: moved {moved} materials")
