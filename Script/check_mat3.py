import unreal
mesh = unreal.EditorAssetLibrary.load_asset("/Game/Sekiro/Character/c0000/c0000")
mats = mesh.get_editor_property("materials")
count = 0
for sm in mats:
    mi = sm.material_interface
    if mi:
        name = mi.get_name()
        if "Armor" in name or "armor" in name.lower(): count += 1
print("Materials with Armor: " + str(count))
print("Total slots: " + str(len(mats)))