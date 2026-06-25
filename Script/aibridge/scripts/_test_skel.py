import unreal
# Get skeleton directly by known path
skel = unreal.load_asset("/Game/Characters/Sekiro/Models/Sekiro_Skeleton.Sekiro_Skeleton")
print("skel type: " + str(type(skel)))
if skel:
    props = [x for x in dir(skel) if not x.startswith("_")]
    print("\n".join(props))
else:
    print("Not found, trying alternative paths...")
    paths = [
        "/Game/Characters/Sekiro/Animations/Sekiro_Skeleton",
        "/Game/Characters/Sekiro/Models/Sekiro_Skeleton",
        "/Game/Characters/Sekiro/Character/Sekiro_Skeleton",
    ]
    for p in paths:
        a = unreal.load_asset(p)
        print(p + ": " + str(a))
