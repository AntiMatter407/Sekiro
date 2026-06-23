import unreal
mesh = unreal.EditorAssetLibrary.load_asset("/Game/Sekiro/Character/c0000/c0000")
mats = mesh.get_editor_property("materials")
print("Slots: " + str(len(mats)))
assigned = 0
for i, sm in enumerate(mats[:10]):
    mi = sm.material_interface
    name = (mi.get_path_name() if mi else "none")
    print("  [" + str(i) + "] " + name)
    if mi: assigned += 1
for sm in mats[10:]:
    if sm.material_interface: assigned += 1
print("Assigned: " + str(assigned) + "/" + str(len(mats)))