import unreal

base = "/Game/Sekiro/Character/c0000"
eal = unreal.EditorAssetLibrary

# Create Materials folder
if not eal.does_directory_exist(base + "/Materials"):
    eal.make_directory(base + "/Materials")

# List all M_ assets at base level
assets = eal.list_assets(base, recursive=False)
m_assets = [a for a in assets if a.split("/")[-1].startswith("M_")]
print(f"Found {len(m_assets)} M_ assets at root")

# Try duplication instead of rename
at = unreal.AssetToolsHelpers.get_asset_tools()
moved = 0
for path in m_assets:
    name = path.split("/")[-1]
    new_path = base + "/Materials/" + name
    if not eal.does_asset_exist(new_path):
        src = eal.load_asset(path)
        if src:
            # Duplicate to new location
            dup = at.duplicate_asset(name + "_dup", base + "/Materials", src)
            if dup:
                # Rename duplicate to proper name
                eal.rename_asset(dup.get_path_name(), new_path)
                # Delete original
                eal.delete_asset(path)
                moved += 1
                print(f"Moved: {name}")
print(f"Total moved: {moved}")
