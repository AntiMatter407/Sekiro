
import unreal

base_path = "/Game/Sekiro/Character/c0000/Textures"
EditorAssetLibrary = unreal.EditorAssetLibrary

assets = EditorAssetLibrary.list_assets(base_path, recursive=False)

normal_count = 0
linear_count = 0
for asset_path in assets:
    name = asset_path.split("/")[-1].lower()
    tex = EditorAssetLibrary.load_asset(asset_path)
    if not tex or not isinstance(tex, unreal.Texture2D):
        continue
    
    if name.endswith("_n") or name.endswith("_n.png"):
        # Normal map
        tex.compression_settings = unreal.TextureCompressionSettings.TC_NORMALMAP
        normal_count += 1
    elif any(name.endswith(s) for s in ("_m", "_r", "_ao", "_mask", "_1m")):
        # Linear/Masks
        tex.compression_settings = unreal.TextureCompressionSettings.TC_MASKS
        tex.srgb = False
        linear_count += 1
    elif name.endswith("_em"):
        tex.srgb = False
        linear_count += 1
    
    EditorAssetLibrary.save_asset(asset_path)

print(f"Fixed: {normal_count} normals, {linear_count} linear/grayscale")
