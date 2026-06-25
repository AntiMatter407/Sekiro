import unreal
skeleton = unreal.load_asset("/Game/Characters/Sekiro/Sekiro_Skeleton.Sekiro_Skeleton")
unreal.log("skeleton: " + str(skeleton))
unreal.log("dir: " + str([x for x in dir(skeleton) if not x.startswith("_") and not x.startswith("set_")]))
bt = skeleton.get_editor_property("bone_tree")
unreal.log("bone_tree: " + str(bt) + " len=" + str(len(bt)))
# Try to load the skeleton's reference skeleton via preview mesh
mesh = unreal.load_asset("/Game/Characters/Sekiro/Sekiro_Model.Sekiro_Model")
unreal.log("mesh: " + str(mesh))
if mesh:
    ref_skel = mesh.get_editor_property("skeleton")
    unreal.log("mesh skeleton: " + str(ref_skel))
    # Get bone names from mesh
    bp = mesh.get_editor_property("bone_palette")
    unreal.log("bone_palette: " + str(len(bp)))
    for b in bp[:3]:
        unreal.log("  " + str(b))
