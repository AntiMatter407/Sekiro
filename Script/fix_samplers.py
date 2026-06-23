import unreal
base_path = "/Game/Sekiro/Character/c0000/Textures"
eal = unreal.EditorAssetLibrary
assets = eal.list_assets(base_path, recursive=False)
normal_count = 0
linear_count = 0
for path in assets:
    name = path.split("/")[-1].lower()
    tex = eal.load_asset(path)
    if not tex or not isinstance(tex, unreal.Texture2D): continue
    if name.endswith("_n") or name.endswith("_n.png"):
        tex.compression_settings = unreal.TextureCompressionSettings.TC_NORMALMAP
        normal_count += 1
    elif any(name.endswith(s) for s in ("_m", "_r", "_ao", "_mask", "_1m")):
        tex.compression_settings = unreal.TextureCompressionSettings.TC_MASKS
        tex.srgb = False
        linear_count += 1
    elif name.endswith("_em"):
        tex.srgb = False
        linear_count += 1
    eal.save_asset(path)
print("Fixed: " + str(normal_count) + " normals, " + str(linear_count) + " linear/grayscale")