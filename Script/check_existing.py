import unreal
eal = unreal.EditorAssetLibrary
base = "/Game/Sekiro/Character/c0000/Textures"
existing = set(eal.list_assets(base, recursive=False))
print("Existing textures: " + str(len(existing)))