import unreal
mat = unreal.EditorAssetLibrary.load_asset("/Game/Sekiro/Character/c0000/c0000/Materials/M_BD_M_9000_tops1l")
if mat:
    params = mat.get_editor_property("texture_parameter_values")
    print("Texture params: " + str(len(params)) if params else "0")
    for p in (params or []):
        name = p.get_editor_property("parameter_info").get_editor_property("name")
        tex = p.get_editor_property("parameter_value")
        print("  " + str(name) + " -> " + (tex.get_name() if tex else "none"))
else:
    print("Not loaded")