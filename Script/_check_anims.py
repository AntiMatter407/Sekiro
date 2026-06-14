"""Quick check: all animations by category for replacement candidates."""
import unreal

ANIM_PATH = "/Game/Characters/Sekiro/Animations"
assets = unreal.EditorAssetLibrary.list_assets(ANIM_PATH, recursive=False)

results = []
for a in assets:
    path_str = str(a)
    name_only = path_str.split("/")[-1].split(".")[-1] if "." in path_str else path_str.split("/")[-1]
    try:
        anim = unreal.EditorAssetLibrary.load_asset(path_str)
        if not anim:
            continue
        if anim.get_class().get_name() != "AnimSequence":
            continue
        loop = anim.get_editor_property("bLoop")
        length = anim.get_editor_property("sequence_length")
        results.append((name_only, length, loop))
    except:
        pass

# Print loops only
results.sort()
for name, length, loop in results:
    if loop and length > 0.5:
        unreal.log(f"LOOP {name}: {length:.2f}s")
