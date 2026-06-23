import unreal

base = "/Game/Sekiro/Character/c0000"
eal = unreal.EditorAssetLibrary

# Create Materials folder
if not eal.does_directory_exist(base + "/Materials"):
    eal.make_directory(base + "/Materials")

# Use hardcoded material names from the bridge query
mat_names = [
    "M_AM_M_9000_tops", "M_AM_M_9000_tilingset2", "M_AM_M_9000_tilingset1",
    "M_AM_M_9000_tilingrope", "M_AM_M_9000_tilingchain", "M_AM_M_9000_tilingbandage",
    "M_AM_M_9000_chain", "M_AM_M_9000_body", "M_AM_M_9000_artificialarm", "M_AM_M_9000__00",
    "M_BD_M_9000_topsR_cloth", "M_BD_M_9000_tops1l", "M_BD_M_9000_tilingset1",
    "M_BD_M_9000_tilingrope", "M_BD_M_9000_tilingchain", "M_BD_M_9000_muffler_cloth",
    "M_BD_M_9000_muffler01_cloth", "M_BD_M_9000_muffler", "M_BD_M_9000_Material__520",
    "M_BD_M_9000_Material__516", "M_BD_M_9000_Material__515", "M_BD_M_9000_fray1",
    "M_BD_M_9000_fraryR_cloth", "M_BD_M_9000_Court_Material", "M_BD_M_9000_body",
    "M_FC_M_0100_Material__33", "M_FC_M_0100_Material__31", "M_FC_M_0100_Material__29",
    "M_FC_M_0100_Material__27", "M_FC_M_0100_Material__165", "M_FC_M_0100_Material__163",
    "M_FC_M_0100_Material__151", "M_FC_M_0100_Material__149", "M_FC_M_0100_hair02_cloth",
    "M_FC_M_0100_FC_M_0100_Eye", "M_FC_M_0100_ck",
    "M_HD_M_9510_Material__168", "M_HD_M_9520_Material__168",
    "M_LG_M_9000_bottoms2", "M_LG_M_9000_bottoms1", "M_LG_M_9000_tilingbandage",
    "M_LG_M_9000_tilingchain", "M_LG_M_9000_tilingrope", "M_LG_M_9000_fray1",
    "M_LG_M_9000_tilingset1"
]

moved = 0
for name in mat_names:
    old_path = base + "/" + name
    new_path = base + "/Materials/" + name
    if eal.does_asset_exist(old_path) and not eal.does_asset_exist(new_path):
        eal.rename_asset(old_path, new_path)
        moved += 1
        print(f"Moved: {name}")

print(f"Total moved: {moved}/45")

# Update mesh material slots
mesh = eal.load_asset(base + ".c0000")
if mesh:
    mats = mesh.get_editor_property("materials")
    updated = 0
    for i, m in enumerate(mats):
        if m.material_interface:
            old_name = m.material_interface.get_name()
            new_path = base + "/Materials/" + old_name
            new_mat = eal.load_asset(new_path)
            if new_mat:
                mats[i].material_interface = new_mat
                updated += 1
    mesh.set_editor_property("materials", mats)
    eal.save_asset(base + ".c0000")
    print(f"Updated {updated} mesh material slots")
