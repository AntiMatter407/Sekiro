"""Configure BP_SekiroCharacter's WeaponComponent to use BP_Kusabimaru."""
import unreal

CHAR_BP_PATH = '/Game/Gameplay/BP_SekiroCharacter'
WEAPON_BP_PATH = '/Game/Weapons/Kusabimaru/BP_Kusabimaru'


def main():
    char_bp = unreal.EditorAssetLibrary.load_asset(CHAR_BP_PATH)
    if not char_bp:
        unreal.log_error('Character BP not found: ' + CHAR_BP_PATH)
        return

    weapon_bp = unreal.EditorAssetLibrary.load_asset(WEAPON_BP_PATH)
    if not weapon_bp:
        unreal.log_error('Weapon BP not found: ' + WEAPON_BP_PATH)
        return

    gc = char_bp.generated_class()
    try:
        cdo = gc.get_default_object()
    except Exception:
        cdo = gc.GetDefaultObject()

    # Access WeaponComponent
    wc = None
    try:
        wc = cdo.get_editor_property('WeaponComponent')
    except Exception:
        pass

    if wc is None:
        # Try finding via components
        try:
            comps = cdo.get_components_by_class(unreal.ActorComponent)
            for c in comps:
                name = c.get_name()
                if 'Weapon' in name:
                    wc = c
                    unreal.log('Found weapon component: ' + name)
                    break
        except Exception:
            pass

    if wc is None:
        unreal.log_error('WeaponComponent not found on character CDO!')
        return

    # Set DefaultWeaponClass to the weapon Blueprint class
    weapon_gc = weapon_bp.generated_class()
    wc.set_editor_property('DefaultWeaponClass', weapon_gc)
    unreal.log('Set DefaultWeaponClass to BP_Kusabimaru')

    # Verify socket name
    try:
        sn = wc.get_editor_property('AttachSocketName')
        unreal.log('Socket name: ' + str(sn))
    except Exception:
        pass

    unreal.EditorAssetLibrary.save_asset(CHAR_BP_PATH)
    unreal.log('=== Character weapon setup COMPLETE ===')


main()
