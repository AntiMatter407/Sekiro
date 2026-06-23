import unreal
mat = unreal.EditorAssetLibrary.load_asset("/Game/Sekiro/Character/c0000/Materials/M_AM_M_9000_tilingbandage")
if mat:
    exps = mat.get_editor_property("expressions")
    for e in exps:
        if "TextureSample" in e.get_class().get_name():
            tex = e.get_editor_property("texture")
            print(e.get_class().get_name() + ": " + (tex.get_name() if tex else "none"))
else: print("not loaded")