"""
只狼动画逻辑数据构建管线
=========================
从 TAE JSON 构建 USKAnimationLogicData DataAsset，运行时组件只读资产，不碰 JSON。
"""

import unreal


def build_anim_logic_data():
    """加载 TAE JSON，构建 USKAnimationLogicData DataAsset 并保存。"""

    json_path = unreal.Paths.project_dir() + "Extracted/Sekiro_TAE_Logic.json"
    package_path = "/Game/Characters/Sekiro"
    asset_name = "SK_AnimLogicData"
    full_path = package_path + "/" + asset_name

    unreal.log("=" * 60)
    unreal.log("[build_anim_data] 开始构建动画逻辑 DataAsset")
    unreal.log("[build_anim_data] JSON: " + json_path)
    unreal.log("[build_anim_data] 目标路径: " + full_path)

    # ── 1. 导入 TAE JSON → ABIR ──────────────────────────────────
    unreal.log("[build_anim_data] 步骤 1/3: 导入 TAE JSON → ABIR...")
    result = unreal.FSKAnimLogicImportResult()
    unreal.SekiroImportLibrary.import_tae_logic(json_path, result)
    unreal.log(f"[build_anim_data] IR 加载完成: {result.total_anims} 动画, {result.total_events} 事件")

    if result.total_anims == 0:
        unreal.log_error("[build_anim_data] 错误: IR 中无动画数据，请检查 JSON 路径")
        return None

    # ── 2. 构建 DataAsset ────────────────────────────────────────
    unreal.log("[build_anim_data] 步骤 2/3: 构建 DataAsset...")
    da = unreal.SekiroImportLibrary.build_anim_logic_data_asset(
        result, package_path, asset_name
    )
    if not da:
        unreal.log_error("[build_anim_data] 错误: DataAsset 构建失败")
        return None

    unreal.log(f"[build_anim_data] DataAsset 构建完成: {da}")

    # ── 3. 保存 DataAsset ────────────────────────────────────────
    unreal.log("[build_anim_data] 步骤 3/3: 保存 DataAsset...")
    saved = unreal.EditorAssetLibrary.save_loaded_asset(da)
    if not saved:
        unreal.log_error("[build_anim_data] 错误: DataAsset 保存失败")
        return None

    unreal.log(f"[build_anim_data] DataAsset 已保存: {full_path}")

    # ── 4. 验证覆盖率 ────────────────────────────────────────────
    unreal.log("[build_anim_data] 验证...")
    validate_data_asset(da, result.total_anims)

    unreal.log("[build_anim_data] 完成！")
    return da


def validate_data_asset(da, expected_anim_count):
    """验证 DataAsset 数据覆盖率和完整性。"""

    checks_passed = 0
    checks_total = 0

    # 动画覆盖率
    checks_total += 1
    name_count = len(da.anim_name_map)
    if name_count == expected_anim_count:
        unreal.log(f"[验证] PASS: 动画覆盖率 100% ({name_count}/{expected_anim_count})")
        checks_passed += 1
    else:
        unreal.log_warning(f"[验证] WARN: 动画覆盖率 {name_count}/{expected_anim_count}")

    # CancelRules 条目数
    checks_total += 1
    cancel_count = len(da.cancel_rules)
    if cancel_count > 0:
        unreal.log(f"[验证] PASS: CancelRules 条目数 = {cancel_count}")
        checks_passed += 1
    else:
        unreal.log_warning("[验证] WARN: CancelRules 为空")

    # AttackHitboxConfigs 条目数
    checks_total += 1
    hitbox_count = len(da.attack_hitbox_configs)
    if hitbox_count > 0:
        unreal.log(f"[验证] PASS: AttackHitboxConfigs 条目数 = {hitbox_count}")
        checks_passed += 1
    else:
        unreal.log_warning("[验证] WARN: AttackHitboxConfigs 为空")

    # AnimFrameFlags 条目数
    checks_total += 1
    flags_count = len(da.anim_frame_flags)
    if flags_count > 0:
        unreal.log(f"[验证] PASS: AnimFrameFlags 条目数 = {flags_count}")
        checks_passed += 1
    else:
        unreal.log_warning("[验证] WARN: AnimFrameFlags 为空")

    # SpEffectConfigs 条目数
    checks_total += 1
    speff_count = len(da.sp_effect_configs)
    if speff_count > 0:
        unreal.log(f"[验证] PASS: SpEffectConfigs 条目数 = {speff_count}")
        checks_passed += 1
    else:
        unreal.log_warning("[验证] WARN: SpEffectConfigs 为空（预期行为）")

    # CategoryAnimMap 类别数
    checks_total += 1
    cat_count = len(da.category_anim_map)
    if cat_count > 0:
        unreal.log(f"[验证] PASS: CategoryAnimMap 类别数 = {cat_count}")
        checks_passed += 1
    else:
        unreal.log_warning("[验证] WARN: CategoryAnimMap 为空")

    unreal.log(f"[验证] 结果: {checks_passed}/{checks_total} 项通过")


if __name__ == "__main__":
    build_anim_logic_data()
