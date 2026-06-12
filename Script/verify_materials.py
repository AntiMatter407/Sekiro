"""
材质验证 — UE5.2 Python API 最终版
属性路径: MIC.BasePropertyOverrides.{BlendMode, TwoSided, bOverride_*}
"""
import unreal

MAT_PATH = "/Game/Characters/Sekiro/Materials"
TEX_PATH = "/Game/Characters/Sekiro/Textures"

all_assets = unreal.EditorAssetLibrary.list_assets(MAT_PATH, recursive=True)
mic_paths = [a for a in all_assets if "MI_" in a]
unreal.log(f"=== 材质验证: {len(mic_paths)} 个MIC ===")

blend_stats = {}
twosided_n = 0
tex_ok = 0
tex_missing = 0
samples = []
issues = []

# 已知贴图参数名
REQUIRED_PARAMS = ["_a", "_n", "_m"]

for i, path in enumerate(mic_paths):
    mic = unreal.load_asset(path)
    if not mic:
        continue
    name = mic.get_name()

    # BasePropertyOverrides
    bp = mic.get_editor_property("BasePropertyOverrides")
    blend = bp.get_editor_property("BlendMode")
    twosided = bp.get_editor_property("TwoSided")
    b_blend_ov = bp.get_editor_property("bOverride_BlendMode")
    b_twosid_ov = bp.get_editor_property("bOverride_TwoSided")

    blend_name = str(blend).split(".")[-1].split(":")[0].strip()
    blend_stats[blend_name] = blend_stats.get(blend_name, 0) + 1
    if twosided:
        twosided_n += 1

    # 贴图参数
    tex_vals = mic.get_editor_property("TextureParameterValues")
    assigned = {}
    if tex_vals:
        for tv in tex_vals:
            pname = tv.get_editor_property("parameter_info").get_editor_property("name")
            pval = tv.get_editor_property("parameter_value")
            assigned[pname] = pval.get_name() if pval else "None"

    # 检查必要贴图
    missing = [p for p in REQUIRED_PARAMS if p not in assigned or assigned[p] == "None"]
    if missing:
        tex_missing += 1
        issues.append(f"{name}: 缺失贴图 {missing}")
    else:
        tex_ok += 1

    # 检查 BlendOverride 是否在需要时启用
    if blend_name != "BLEND_OPAQUE" and not b_blend_ov:
        issues.append(f"{name}: BlendMode={blend_name} 但 bOverride_BlendMode=False")

    if i < 8:
        tex_info = ", ".join(f"{k}={v}" for k, v in sorted(assigned.items()))
        samples.append(f"  {name}: {blend_name}, 2S={twosided}, "
                       f"bBlend={b_blend_ov}, b2S={b_twosid_ov}, Tex=[{tex_info}]")

# 输出
unreal.log("--- BlendMode分布 ---")
for k in sorted(blend_stats):
    unreal.log(f"  {k}: {blend_stats[k]}")
unreal.log(f"TwoSided: {twosided_n}/{len(mic_paths)}")
unreal.log(f"贴图完整: {tex_ok}, 缺失: {tex_missing}")

unreal.log("--- 样本 (前8个) ---")
for s in samples:
    unreal.log(s)

if issues:
    unreal.log_warning(f"--- 问题 ({len(issues)}个) ---")
    for iss in issues[:10]:
        unreal.log_warning(f"  {iss}")

tex_count = len(unreal.EditorAssetLibrary.list_assets(TEX_PATH, recursive=False))
unreal.log(f"=== 完成: {len(mic_paths)} MIC, {tex_count} 贴图, {len(issues)} 问题 ===")
