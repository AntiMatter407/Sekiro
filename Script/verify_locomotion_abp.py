"""
Sekiro Locomotion ABP 验证脚本
在 UE5 编辑器中执行：Window -> Python Console -> 粘贴运行
或通过命令行：
  UnrealEditor-Cmd.exe Sekiro.uproject -run=pythonscript -script="F:/ProjectAI/Sekiro/Script/verify_locomotion_abp.py"

验证步骤：
  1. 运行 SekiroImport.BuildAnimBlueprint 命令构建 ABP
  2. 验证 SK_Locomotion_BS BlendSpace（类型、轴范围、采样点）
  3. 验证 ABP_Sekiro 父类是否是 USKAnimInstance
  4. 验证 BP_SekiroCharacter.Mesh.AnimClass 是否指向 ABP_Sekiro
"""

import unreal
import time

# ================================================================
# 常量
# ================================================================
JSON_PATH = "F:/ProjectAI/Sekiro/Extracted/Sekiro_TAE_Logic.json"
OUTPUT_PATH = "/Game/Characters/Sekiro"

BLENDSPACE_PATH = f"{OUTPUT_PATH}/SK_Locomotion_BS"
ABP_PATH = f"{OUTPUT_PATH}/ABP_Sekiro"
CHARACTER_PATH = "/Game/Gameplay/BP_SekiroCharacter"

# 正确的 AnimInstance 类路径（对应 USKAnimInstance）
SK_ANIM_INSTANCE_PATH = "/Script/Sekiro.SKAnimInstance"
# 插件代码中错误使用的路径（用于检测已知 bug）
SK_ANIM_INSTANCE_PATH_BUG = "/Script/Sekiro.SekiroAnimInstance"

# 等待配置
MAX_RETRIES = 120       # 最大重试秒数
RETRY_INTERVAL = 1.0    # 轮询间隔（秒）
COMMAND_GRACE_PERIOD = 3.0  # 命令执行后的初始等待（命令内部通过 FTSTicker 异步执行）


# ================================================================
# 日志辅助函数
# ================================================================

def log_pass(msg):
    """打印成功标记"""
    unreal.log(f"  [PASS] {msg}")


def log_fail(msg):
    """打印失败标记"""
    unreal.log_error(f"  [FAIL] {msg}")


def log_warn(msg):
    """打印警告标记"""
    unreal.log_warning(f"  [WARN] {msg}")


def log_info(msg):
    """打印普通信息"""
    unreal.log(f"  [INFO] {msg}")


def log_header(msg):
    """打印分隔标题"""
    unreal.log("=" * 60)
    unreal.log(f"  {msg}")
    unreal.log("=" * 60)


# ================================================================
# 资产加载（参考 create_abp.py 的 load_asset 模式）
# ================================================================

def load_asset(path, asset_type=None):
    """加载资产，尝试多种方式"""
    asset_name = path.split('/')[-1]
    full_path = f"{path}.{asset_name}"

    # 方法1: load_object with full path (e.g. /Game/.../Asset.Asset)
    try:
        obj = unreal.load_object(None, full_path)
        if obj and hasattr(obj, 'get_class'):
            cls = obj.get_class()
            if cls and cls.get_name() != 'Package':
                return obj
    except:
        pass

    # 方法2: 强制加载包裹，然后查找内部对象
    try:
        pkg = unreal.find_package(None, path)
        if not pkg:
            pkg = unreal.load_package(path)
        if pkg:
            obj = unreal.EditorAssetLibrary.load_asset(path)
            if obj:
                return obj
    except:
        pass

    # 方法3: 直接 EditorAssetLibrary
    try:
        obj = unreal.EditorAssetLibrary.load_asset(path)
        if obj:
            return obj
    except:
        pass

    return None


# ================================================================
# 控制台命令与等待
# ================================================================

def _get_editor_world():
    """获取编辑器 World 上下文（用于执行控制台命令）"""
    try:
        editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        return editor_subsystem.get_editor_world()
    except:
        pass
    try:
        return unreal.EditorLevelLibrary.get_editor_world()
    except:
        pass
    return None


def _scan_asset_registry():
    """强制扫描 AssetRegistry"""
    try:
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        ar.search_all_assets(True)
        log_info("AssetRegistry scan 已触发")
    except Exception as e:
        log_warn(f"AssetRegistry scan 失败: {e}")


def execute_build_command():
    """执行 SekiroImport.BuildAnimBlueprint 控制台命令"""
    command = f"SekiroImport.BuildAnimBlueprint {JSON_PATH} {OUTPUT_PATH}"
    world = _get_editor_world()

    try:
        if world:
            unreal.SystemLibrary.execute_console_command(world, command)
        else:
            # 无 World 上下文时（如 Commandlet 模式），尝试直接执行
            unreal.SystemLibrary.execute_console_command(None, command)
        log_info(f"命令已发出: {command}")
        return True
    except Exception as e:
        log_fail(f"无法执行控制台命令: {e}")
        return False


def wait_for_asset(path, timeout=MAX_RETRIES):
    """轮询等待资产加载完成，返回资产对象或 None"""
    log_info(f"等待资产: {path}")
    for attempt in range(1, timeout + 1):
        obj = load_asset(path)
        if obj:
            log_info(f"资产已就绪 (耗时 ~{attempt}s): {path}")
            return obj
        if attempt == 1 or attempt % 15 == 0:
            log_info(f"  等待中... ({attempt}/{timeout}s)")
        time.sleep(RETRY_INTERVAL)

    log_fail(f"等待超时 ({timeout}s): {path}")
    return None


def wait_for_asset_registry(timeout=30):
    """等待 AssetRegistry 完成初始扫描"""
    log_info("等待 AssetRegistry 就绪...")
    try:
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
    except Exception:
        log_warn("无法获取 AssetRegistry，跳过等待")
        return True

    for attempt in range(1, timeout + 1):
        if not ar.is_loading_assets():
            log_info(f"AssetRegistry 就绪 (耗时 ~{attempt}s)")
            return True
        if attempt == 1 or attempt % 10 == 0:
            log_info(f"  AssetRegistry 扫描中... ({attempt}/{timeout}s)")
        time.sleep(RETRY_INTERVAL)

    log_warn(f"AssetRegistry 未在 {timeout}s 内完成扫描，继续执行")
    return False


# ================================================================
# Step 1: 运行构建命令
# ================================================================

def step1_run_build():
    """执行 SekiroImport.BuildAnimBlueprint 并等待资产生成"""
    log_header("Step 1: 运行 SekiroImport.BuildAnimBlueprint")

    if not execute_build_command():
        return False

    # 命令内部通过 FTSTicker 异步执行，等待初始调度
    log_info(f"等待 {COMMAND_GRACE_PERIOD}s 让 FTSTicker 启动...")
    time.sleep(COMMAND_GRACE_PERIOD)

    # 等待 AssetRegistry 完成初始扫描（首次启动编辑器时需要）
    wait_for_asset_registry()

    # 等待产出资产
    bs = wait_for_asset(BLENDSPACE_PATH)
    abp = wait_for_asset(ABP_PATH)

    if bs and abp:
        log_pass("构建命令执行成功: BlendSpace + AnimBlueprint 已生成")
        return True
    else:
        if not bs:
            log_fail("BlendSpace 未生成: SK_Locomotion_BS")
        if not abp:
            log_fail("AnimBlueprint 未生成: ABP_Sekiro")
        return False


# ================================================================
# Step 2: 验证 SK_Locomotion_BS BlendSpace
# ================================================================

def step2_verify_blendspace():
    """加载 SK_Locomotion_BS，打印类型、轴范围、采样点"""
    log_header("Step 2: 验证 SK_Locomotion_BS BlendSpace")

    bs = load_asset(BLENDSPACE_PATH)
    if not bs:
        log_fail(f"无法加载 BlendSpace: {BLENDSPACE_PATH}")
        return False

    all_ok = True

    # --- 2a. BlendSpace 类型 ---
    bs_class = bs.get_class()
    bs_class_name = bs_class.get_name()
    if bs_class_name == "BlendSpace1D":
        log_pass(f"BlendSpace 类型: 1D ({bs_class_name})")
    elif bs_class_name == "BlendSpace":
        log_pass(f"BlendSpace 类型: 2D ({bs_class_name})")
    else:
        log_warn(f"BlendSpace 类型未知: {bs_class_name}")
        all_ok = False

    # --- 2b. Speed 轴范围（Min/Max）---
    # BlendSpace1D 只有一个轴参数 BlendParameters[0]
    speed_min = None
    speed_max = None
    speed_display_name = None

    # 尝试方式1: get_editor_property("blend_parameters")
    try:
        blend_params = bs.get_editor_property("blend_parameters")
        if blend_params and len(blend_params) > 0:
            param0 = blend_params[0]
            # FBlendParameter 结构体
            if hasattr(param0, 'min'):
                speed_min = param0.min
                speed_max = param0.max
                speed_display_name = param0.display_name if hasattr(param0, 'display_name') else ""
    except:
        pass

    # 尝试方式2: get_editor_property("blend_parameter") (1D 别名)
    if speed_min is None:
        try:
            param = bs.get_editor_property("blend_parameter")
            if param:
                speed_min = param.min
                speed_max = param.max
                speed_display_name = param.display_name if hasattr(param, 'display_name') else ""
        except:
            pass

    # 尝试方式3: 直接反射访问 BlendParameters[0]
    if speed_min is None:
        try:
            for attr_name in ["blend_parameter_x", "blend_parameter", "blend_parameters"]:
                try:
                    param = bs.get_editor_property(attr_name)
                    if param:
                        if hasattr(param, '__iter__') and not isinstance(param, str):
                            param = param[0] if len(param) > 0 else param
                        speed_min = float(param.min)
                        speed_max = float(param.max)
                        speed_display_name = str(param.display_name)
                        break
                except:
                    continue
        except:
            pass

    if speed_min is not None and speed_max is not None:
        log_pass(f"Speed 轴范围: Min={speed_min:.0f}, Max={speed_max:.0f}, DisplayName=\"{speed_display_name}\"")
    else:
        log_warn("无法读取 BlendParameters 轴范围（反射属性可能未暴露到 Python）")
        # 尝试从采样点推断范围
        log_info("  将尝试从采样点数据推断范围...")

    # --- 2c. 采样点列表 ---
    # get_blend_samples() 返回 TArray<FBlendSample>
    try:
        samples = bs.get_blend_samples()
    except Exception as e:
        log_fail(f"无法读取 BlendSamples: {e}")
        samples = []

    if not samples or len(samples) == 0:
        log_fail("BlendSpace 无采样点！构建可能不完整")
        all_ok = False
    else:
        log_info(f"采样点数量: {len(samples)}")
        log_info("-" * 50)
        inferred_min = float('inf')
        inferred_max = float('-inf')

        for i, sample in enumerate(samples):
            try:
                # FBlendSample 结构
                anim = sample.animation          # UAnimSequence
                sample_value = sample.sample_value  # FVector (1D 只用 X)
                speed = sample_value.x

                anim_name = anim.get_name() if anim else "(null)"
                inferred_min = min(inferred_min, speed)
                inferred_max = max(inferred_max, speed)

                # AnimID 不直接存储在 BlendSpace 采样点上，
                # 需要从 SK_AnimLogicData DataAsset 交叉引用
                log_info(f"  [{i}] Speed={speed:6.0f}  Anim=\"{anim_name}\"")
            except Exception as e:
                log_warn(f"  [{i}] 读取采样点失败: {e}")

        log_info("-" * 50)

        # 如果无法从 BlendParameters 读取范围，使用推断值
        if speed_min is None:
            log_info(f"Speed 范围（从采样点推断）: Min={inferred_min:.0f}, Max={inferred_max:.0f}")

        log_info("注意: AnimID 不存储在 BlendSpace 采样点上，请查阅 SK_AnimLogicData DataAsset 获取 AnimID 映射")

    return all_ok


# ================================================================
# Step 3: 验证 ABP_Sekiro 父类
# ================================================================

def step3_verify_abp_parent_class():
    """加载 ABP_Sekiro，验证父类是否是 USKAnimInstance"""
    log_header("Step 3: 验证 ABP_Sekiro 父类")

    abp = load_asset(ABP_PATH)
    if not abp:
        log_fail(f"无法加载 AnimBlueprint: {ABP_PATH}")
        return False

    abp_name = abp.get_name()
    log_info(f"已加载: {abp_name}")

    all_ok = True

    # --- 3a. 获取当前父类 ---
    try:
        parent_class = abp.get_editor_property("parent_class")
    except:
        try:
            parent_class = abp.parent_class
        except:
            parent_class = None

    if parent_class is None:
        log_fail("ABP_Sekiro 的 ParentClass 为 None")
        all_ok = False
    else:
        parent_class_name = parent_class.get_name()
        parent_class_path = parent_class.get_path_name() if hasattr(parent_class, 'get_path_name') else parent_class_name
        log_info(f"当前 ParentClass: {parent_class_name}")
        log_info(f"  ClassPath: {parent_class_path}")

    # --- 3b. 加载期望的类 ---
    expected_class = None
    # 使用正确的类路径 /Script/Sekiro.SKAnimInstance
    try:
        expected_class = unreal.load_class(None, SK_ANIM_INSTANCE_PATH)
    except Exception as e:
        log_warn(f"无法加载 {SK_ANIM_INSTANCE_PATH}: {e}")

    if expected_class is None:
        log_fail(f"期望的 USKAnimInstance 类未找到: {SK_ANIM_INSTANCE_PATH}")
        all_ok = False
    else:
        log_info(f"期望类已加载: {expected_class.get_name()}")

    # --- 3c. 比较 ---
    if parent_class is not None and expected_class is not None:
        if parent_class == expected_class:
            log_pass("ABP_Sekiro 父类正确: USKAnimInstance (/Script/Sekiro.SKAnimInstance)")
        else:
            log_fail(f"父类不匹配！当前={parent_class.get_name()}，期望=USKAnimInstance")
            all_ok = False

            # 检测已知 bug: 插件代码错误地使用了 /Script/Sekiro.SekiroAnimInstance
            log_info("诊断: 插件 SekiroAnimBlueprintBuilder.cpp 第185行使用了错误路径:")
            log_info(f"  当前代码: LoadClass(\"{SK_ANIM_INSTANCE_PATH_BUG}\")")
            log_info(f"  正确应为: LoadClass(\"{SK_ANIM_INSTANCE_PATH}\")")

    return all_ok


# ================================================================
# Step 4: 验证 BP_SekiroCharacter.Mesh.AnimClass
# ================================================================

def step4_verify_character_anim_class():
    """加载 BP_SekiroCharacter，检查 Mesh 组件的 AnimClass 是否指向 ABP_Sekiro"""
    log_header("Step 4: 验证 BP_SekiroCharacter.Mesh.AnimClass")

    bp_char = load_asset(CHARACTER_PATH)
    if not bp_char:
        log_fail(f"无法加载 Character Blueprint: {CHARACTER_PATH}")
        return False

    log_info(f"已加载: {bp_char.get_name()}")

    abp = load_asset(ABP_PATH)
    if not abp:
        log_fail(f"无法加载 ABP 用于比较: {ABP_PATH}")
        return False

    all_ok = True

    # --- 4a. 获取 CDO 和 Mesh 组件 ---
    try:
        generated_class = bp_char.generated_class()
        cdo = unreal.get_default_object(generated_class)
    except Exception as e:
        log_fail(f"无法获取 BP_SekiroCharacter 的 CDO: {e}")
        return False

    try:
        mesh = cdo.get_editor_property("mesh")
    except:
        # 尝试常见别名
        mesh = None
        for comp_name in ["Mesh", "mesh", "SkeletalMesh", "CharacterMesh0"]:
            try:
                mesh = cdo.get_editor_property(comp_name)
                if mesh:
                    break
            except:
                continue

    if not mesh:
        log_fail("BP_SekiroCharacter 未找到 Mesh 组件")
        all_ok = False
    else:
        log_info(f"Mesh 组件: {mesh.get_name()} (类型: {mesh.get_class().get_name()})")

        # --- 4b. 检查 AnimClass ---
        try:
            anim_class = mesh.get_editor_property("anim_class")
        except:
            try:
                anim_class = mesh.anim_class
            except:
                anim_class = None

        if anim_class is None:
            log_warn("Mesh.AnimClass 为 None（未分配动画蓝图）")
            all_ok = False
        else:
            anim_class_name = anim_class.get_name() if anim_class else "(null)"
            log_info(f"Mesh.AnimClass: {anim_class_name}")

            # --- 4c. 与 ABP_Sekiro 的 generated class 比较 ---
            expected_abp_class = abp.generated_class()
            expected_name = expected_abp_class.get_name() if expected_abp_class else "(null)"

            if anim_class == expected_abp_class:
                log_pass(f"Mesh.AnimClass 正确指向 ABP_Sekiro ({expected_name})")
            else:
                log_fail(f"Mesh.AnimClass 不匹配！当前={anim_class_name}，期望={expected_name}")
                all_ok = False

    return all_ok


# ================================================================
# 主流程
# ================================================================

def main():
    unreal.log("")
    unreal.log("#" * 60)
    unreal.log("#  Sekiro Locomotion ABP 验证脚本")
    unreal.log("#" * 60)
    unreal.log(f"#  JSON:   {JSON_PATH}")
    unreal.log(f"#  Output: {OUTPUT_PATH}")
    unreal.log("#" * 60)
    unreal.log("")

    # 预处理: 扫描 AssetRegistry
    _scan_asset_registry()

    # 验证结果汇总
    results = {}

    # Step 1: 运行构建命令
    try:
        results["Build"] = step1_run_build()
    except Exception as e:
        log_fail(f"Step 1 异常: {e}")
        results["Build"] = False

    # Step 2: 验证 BlendSpace
    try:
        results["BlendSpace"] = step2_verify_blendspace()
    except Exception as e:
        log_fail(f"Step 2 异常: {e}")
        results["BlendSpace"] = False

    # Step 3: 验证 ABP 父类
    try:
        results["ABP_ParentClass"] = step3_verify_abp_parent_class()
    except Exception as e:
        log_fail(f"Step 3 异常: {e}")
        results["ABP_ParentClass"] = False

    # Step 4: 验证 Character AnimClass
    try:
        results["Character_AnimClass"] = step4_verify_character_anim_class()
    except Exception as e:
        log_fail(f"Step 4 异常: {e}")
        results["Character_AnimClass"] = False

    # --- 汇总报告 ---
    unreal.log("")
    log_header("验证结果汇总")
    all_pass = True
    for step_name, passed in results.items():
        marker = "[PASS]" if passed else "[FAIL]"
        if not passed:
            all_pass = False
        unreal.log(f"  {marker} {step_name}")

    unreal.log("=" * 60)
    if all_pass:
        unreal.log("  全部验证通过！")
    else:
        failed_count = sum(1 for v in results.values() if not v)
        unreal.log_warning(f"  存在 {failed_count} 项未通过验证，请检查上述 [FAIL] 详情")
    unreal.log("=" * 60)
    unreal.log("")

    return all_pass


if __name__ == "__main__":
    main()
