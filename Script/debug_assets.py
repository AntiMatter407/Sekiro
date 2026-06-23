import unreal

base = "/Game/Sekiro/Character/c0000"
eal = unreal.EditorAssetLibrary

# Debug: what methods exist?
methods = [m for m in dir(eal) if "list" in m.lower() or "asset" in m.lower()]
print("EditorAssetLibrary asset methods:")
for m in methods:
    print(f"  {m}")

# Try list_assets
try:
    assets = eal.list_assets(base, recursive=False)
    print(f"list_assets returned: {len(assets)} items")
except Exception as e:
    print(f"list_assets error: {e}")

# Try does_asset_exist
print(f"does_asset_exist M_AM_M_9000_tops: {eal.does_asset_exist(base + '/M_AM_M_9000_tops')}")
