"""
只狼动画逻辑数据构建管线
=========================
从 TAE JSON 构建 USKAnimationLogicData DataAsset。
"""
import unreal
import re


def build_anim_logic_data():
    json_path = unreal.Paths.project_dir() + "Extracted/Sekiro_TAE_Logic.json"
    package_path = "/Game/Characters/Sekiro"
    asset_name = "DA_Sekiro_AnimLogic"
    full_path = package_path + "/" + asset_name

    unreal.log_warning("[build_anim_data] ===== 开始构建动画逻辑 DataAsset =====")
    unreal.log_warning("[build_anim_data] JSON: " + json_path)

    # ── 1. 导入 TAE JSON ──────────────────────────────────────
    unreal.log_warning("[build_anim_data] 步骤 1/2: 导入 TAE JSON...")
    result = unreal.SekiroAssetManagerBPLibrary.import_tae_logic(json_path)
    if result is None:
        unreal.log_error("[build_anim_data] 错误: TAE 导入返回 None")
        return None

    # UE5 Python 值类型结构体属性通过 export_text 获取
    txt = result.export_text()
    m = re.search(r"TotalAnims=(\d+)", txt)
    n = re.search(r"TotalEvents=(\d+)", txt)
    total_anims = int(m.group(1)) if m else 0
    total_events = int(n.group(1)) if n else 0

    unreal.log_warning("[build_anim_data] IR 加载完成: " + str(total_anims) + " 动画, " + str(total_events) + " 事件")

    if total_anims == 0:
        unreal.log_error("[build_anim_data] 错误: IR 中无动画数据")
        return None

    # ── 2. 构建 DataAsset ────────────────────────────────────
    unreal.log_warning("[build_anim_data] 步骤 2/2: 构建并保存 DataAsset...")
    da = unreal.SekiroAssetManagerBPLibrary.build_anim_logic_data_asset(
        result, package_path, asset_name
    )
    if not da:
        unreal.log_error("[build_anim_data] 错误: DataAsset 构建失败")
        return None

    unreal.log_warning("[build_anim_data] DataAsset 已保存: " + full_path)

    # ── 3. 验证 ──────────────────────────────────────────────
    validate_data_asset(da, total_anims)
    unreal.log_warning("[build_anim_data] 完成！")
    return da


def validate_data_asset(da, expected_anim_count):
    checks_passed = 0
    checks_total = 0

    # CancelRules
    checks_total += 1
    cancel_count = len(da.cancel_rules)
    if cancel_count > 0:
        unreal.log_warning("[验证] PASS: CancelRules 条目 = " + str(cancel_count))
        checks_passed += 1
    else:
        unreal.log_warning("[验证] WARN: CancelRules 为空")

    # AttackHitboxConfigs
    checks_total += 1
    hitbox_count = len(da.attack_hitbox_configs)
    if hitbox_count > 0:
        unreal.log_warning("[验证] PASS: AttackHitboxConfigs 条目 = " + str(hitbox_count))
        checks_passed += 1
    else:
        unreal.log_warning("[验证] WARN: AttackHitboxConfigs 为空")

    # AnimFrameFlags
    checks_total += 1
    flags_count = len(da.anim_frame_flags)
    if flags_count > 0:
        unreal.log_warning("[验证] PASS: AnimFrameFlags 条目 = " + str(flags_count))
        checks_passed += 1
    else:
        unreal.log_warning("[验证] WARN: AnimFrameFlags 为空")

    # CategoryAnimMap
    checks_total += 1
    cat_count = len(da.category_anim_map)
    if cat_count > 0:
        unreal.log_warning("[验证] PASS: CategoryAnimMap 类别 = " + str(cat_count))
        checks_passed += 1
    else:
        unreal.log_warning("[验证] WARN: CategoryAnimMap 为空")

    unreal.log_warning("[验证] 结果: " + str(checks_passed) + "/" + str(checks_total) + " 项通过")


if __name__ == "__main__":
    build_anim_logic_data()
