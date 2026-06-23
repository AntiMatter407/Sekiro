import unreal
mesh = unreal.EditorAssetLibrary.load_asset("/Game/Sekiro/Character/c0000/c0000")
mats = mesh.get_editor_property("materials")
print("Slot count: " + str(len(mats)))
for i, sm in enumerate(mats):
    mi = sm.material_interface
    name = mi.get_path_name() if mi else "none"
    print("[" + str(i) + "] " + name)