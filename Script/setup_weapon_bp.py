"""Setup BP_Kusabimaru: delete old, create with correct parent, set mesh."""
import unreal


def find_sekiro_weapon_class():
    paths = [
        '/Script/Sekiro.SekiroWeapon',
        '/Script/Sekiro.ASekiroWeapon',
        '/Script/Engine.SekiroWeapon',
    ]
    for p in paths:
        try:
            k = unreal.load_class(None, p)
            if k:
                return k
        except Exception:
            pass
        try:
            k = unreal.find_object(None, p)
            if k:
                return k
        except Exception:
            pass
    # Last resort: scan all classes
    try:
        for c in unreal.get_all_loaded_classes():
            n = str(c.get_name())
            if n == 'SekiroWeapon' or n == 'ASekiroWeapon':
                return c
    except Exception:
        pass
    return None


def main():
    bp_path = '/Game/Weapons/Kusabimaru/BP_Kusabimaru'
    sk_path = '/Game/Weapons/Kusabimaru/WP_A_0300_Kusabimaru'

    # 1. Find SekiroWeapon class
    weapon_cls = find_sekiro_weapon_class()
    if not weapon_cls:
        unreal.log_error('FATAL: ASekiroWeapon class not found in reflection. Is C++ compiled?')
        return
    unreal.log('Found weapon class')

    # 2. Delete old BP if parent class is wrong
    if unreal.EditorAssetLibrary.does_asset_exist(bp_path):
        bp = unreal.EditorAssetLibrary.load_asset(bp_path)
        gc = bp.generated_class()
        try:
            parent = gc.get_super_class()
            pname = parent.get_name()
            unreal.log('Existing BP parent: ' + pname)
            if pname != 'SekiroWeapon':
                unreal.log('Deleting BP with wrong parent...')
                unreal.EditorAssetLibrary.delete_asset(bp_path)
        except Exception:
            unreal.log('Could not check parent, recreating...')
            unreal.EditorAssetLibrary.delete_asset(bp_path)

    # 3. Create BP if doesn't exist
    if not unreal.EditorAssetLibrary.does_asset_exist(bp_path):
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', weapon_cls)
        at = unreal.AssetToolsHelpers.get_asset_tools()
        bp = at.create_asset('BP_Kusabimaru', '/Game/Weapons/Kusabimaru', None, factory)
        if not bp:
            unreal.log_error('FATAL: create_asset returned None')
            return
        unreal.EditorAssetLibrary.save_asset(bp_path)
        unreal.log('BP created')

    # 4. Load assets
    bp = unreal.EditorAssetLibrary.load_asset(bp_path)
    sk = unreal.EditorAssetLibrary.load_asset(sk_path)
    if not bp or not sk:
        unreal.log_error('FATAL: failed to load BP or SkeletalMesh')
        return

    # 5. Get CDO and set mesh via component iteration
    gc = bp.generated_class()

    # Try GetDefaultObject with capital G
    cdo = None
    try:
        cdo = gc.get_default_object()
    except Exception:
        pass
    if not cdo:
        try:
            cdo = gc.GetDefaultObject()
        except Exception:
            pass
    if not cdo:
        unreal.log_error('FATAL: cannot get CDO')
        return

    # Find SkeletalMeshComponent on CDO
    mc = None
    try:
        comps = cdo.get_components_by_class(unreal.SkeletalMeshComponent)
        for c in comps:
            mc = c
            break
    except Exception:
        pass

    if not mc:
        unreal.log_error('FATAL: no SkeletalMeshComponent on CDO')
        return

    unreal.log('Found WeaponMesh component')

    # 6. Set skeletal mesh
    set_ok = False
    for method_name in ['set_skinned_asset_and_update', 'set_skinned_asset', 'set_skeletal_mesh']:
        try:
            fn = getattr(mc, method_name, None)
            if fn:
                fn(sk)
                set_ok = True
                unreal.log('Set mesh via ' + method_name)
                break
        except Exception:
            pass

    if not set_ok:
        # Try property assignment
        for prop_name in ['SkinnedAsset', 'SkeletalMesh', 'SkeletalMeshAsset']:
            try:
                mc.set_editor_property(prop_name, sk)
                set_ok = True
                unreal.log('Set mesh via set_editor_property ' + prop_name)
                break
            except Exception:
                pass

    if not set_ok:
        unreal.log_error('FATAL: could not set mesh by any method')
        return

    # 7. Verify socket name
    try:
        sn = cdo.get_editor_property('AttachSocketName')
        unreal.log('AttachSocketName: ' + str(sn))
    except Exception:
        pass

    # 8. Save
    unreal.EditorAssetLibrary.save_asset(bp_path)
    unreal.log('=== BP_Kusabimaru SETUP COMPLETE ===')


main()
