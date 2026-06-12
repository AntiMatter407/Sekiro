import unreal

results = []

# Check M_SekiroBase
m = unreal.EditorAssetLibrary.load_asset('/Game/Characters/Sekiro/Materials/M_SekiroBase')
results.append(f'BaseMat exists: {m is not None}')
if m:
    results.append(f'BaseMat class: {m.get_class().get_name()}')
    results.append(f'bUsedWithSkeletalMesh: {m.get_editor_property("b_used_with_skeletal_mesh")}')

# Check MIC - body (should be Opaque)
mic_body = unreal.EditorAssetLibrary.load_asset('/Game/Characters/Sekiro/Materials/MI_BD_M_9000_body')
if mic_body:
    ov = mic_body.get_editor_property('base_property_overrides')
    results.append(f'MIC_Body Parent: {mic_body.parent.get_name() if mic_body.parent else "None"}')
    results.append(f'MIC_Body BlendMode: {str(ov.blend_mode)}')

# Check MIC - fray1 (should be Masked = cloth keyword)
mic_fray = unreal.EditorAssetLibrary.load_asset('/Game/Characters/Sekiro/Materials/MI_BD_M_9000_fray1')
if mic_fray:
    ov = mic_fray.get_editor_property('base_property_overrides')
    results.append(f'MIC_Fray1 Parent: {mic_fray.parent.get_name() if mic_fray.parent else "None"}')
    results.append(f'MIC_Fray1 BlendMode: {str(ov.blend_mode)}')
    results.append(f'MIC_Fray1 OpacityMaskClipValue: {ov.opacity_mask_clip_value}')

# Check a few more MICs
for name in ['MI_BD_M_9000_muffler', 'MI_BD_M_9000_tops1l', 'MI_LG_M_9000_bottoms1', 'MI_AM_M_9000_body']:
    mic = unreal.EditorAssetLibrary.load_asset(f'/Game/Characters/Sekiro/Materials/{name}')
    if mic:
        ov = mic.get_editor_property('base_property_overrides')
        results.append(f'{name}: Blend={str(ov.blend_mode)}, TwoSided={ov.override_two_sided}')

# Write results
with open('D:/Sekiro/Tools/diagnostic/phase4_verify.txt', 'w', encoding='utf-8') as f:
    f.write('\n'.join(results))
print('Phase 4 verification written')
