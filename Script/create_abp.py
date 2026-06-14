"""
Sekiro 动画蓝图生成脚本
在 UE5 编辑器中执行：Window → Python Console → 粘贴运行
或通过命令行：UnrealEditor-Cmd.exe Sekiro.uproject -run=pythonscript -script="D:/Sekiro/Script/create_abp.py"
"""

import unreal

# ================================================================
# 常量
# ================================================================
OUTPUT_PATH = "/Game/Characters/Sekiro"
LOCOMOTION_ANIMS_PATH = f"{OUTPUT_PATH}/Animations"

# BlendSpace 采样配置: (Speed, Angle, AnimationName)
# 注意: 只加载存在的动画资产
BLENDSPACE_SAMPLES = {
    0: {
        0:  "Anim_Sekiro_Idle_Default",
    },
    150: {
        -180: "Anim_Sekiro_Walk_Bwd",
        -90:  "Anim_Sekiro_Walk_L",
        -45:  "Anim_Sekiro_Walk_Fwd_L",
        0:    "Anim_Sekiro_Walk_Fwd",
        45:   "Anim_Sekiro_Walk_Fwd_R",
        90:   "Anim_Sekiro_Walk_R",
        180:  "Anim_Sekiro_Walk_Bwd",
    },
    300: {
        -45:  "Anim_Sekiro_Run_Fast_Fwd_L",
        0:    "Anim_Sekiro_Run_Fast_Fwd",
        45:   "Anim_Sekiro_Run_Fast_Fwd_R",
    },
    500: {
        -45:  "Anim_Sekiro_Sprint_Fwd_L",
        0:    "Anim_Sekiro_Sprint_Fwd",
        45:   "Anim_Sekiro_Sprint_Fwd_R",
    },
    600: {
        -45:  "Anim_Sekiro_Sprint_Fwd_L",
        0:    "Anim_Sekiro_Sprint_Fwd",
        45:   "Anim_Sekiro_Sprint_Fwd_R",
    },
}


def load_asset(path, asset_type=None):
    """加载资产，尝试多种方式"""
    asset_name = path.split('/')[-1]
    full_path = f"{path}.{asset_name}"

    # 方法1：load_object with full path (e.g. /Game/.../Asset.Asset)
    try:
        obj = unreal.load_object(None, full_path)
        if obj and hasattr(obj, 'get_class'):
            cls = obj.get_class()
            if cls and cls.get_name() != 'Package':
                return obj
    except:
        pass

    # 方法2：强制加载包裹，然后查找内部对象
    try:
        pkg = unreal.find_package(None, path)
        if not pkg:
            pkg = unreal.load_package(path)
        # 通过 EditorAssetLibrary 从已加载包裹中加载
        if pkg:
            obj = unreal.EditorAssetLibrary.load_asset(path)
            if obj:
                return obj
    except:
        pass

    # 方法3：直接 EditorAssetLibrary
    try:
        obj = unreal.EditorAssetLibrary.load_asset(path)
        if obj:
            return obj
    except:
        pass

    return None


def load_anim(asset_name):
    """加载动画资产，不存在返回 None"""
    path = f"{LOCOMOTION_ANIMS_PATH}/{asset_name}"
    return load_asset(path)


def create_blendspace():
    """创建 2D Locomotion BlendSpace"""
    bs_name = "BS_Sekiro_Locomotion"
    bs_path = f"{OUTPUT_PATH}/{bs_name}"

    # 删除已有资产
    if unreal.EditorAssetLibrary.does_asset_exist(bs_path):
        unreal.EditorAssetLibrary.delete_asset(bs_path)
        unreal.log(f"Deleted existing: {bs_path}")

    # 创建 BlendSpace
    skeleton = load_asset(f"{OUTPUT_PATH}/Sekiro_Skeleton")
    if not skeleton:
        unreal.log_error("Skeleton not found!")
        return None

    blendspace = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        bs_name, OUTPUT_PATH, unreal.BlendSpace, unreal.BlendSpaceFactoryNew()
    )
    if not blendspace:
        unreal.log_error("Failed to create BlendSpace!")
        return None

    # 设置目标骨骼 & 混合参数
    blendspace.set_editor_property("target_skeleton", skeleton)
    blendspace.set_editor_property("preview_skeleton", skeleton)
    blendspace.modify()

    # 2D: X=Direction (-180~180, Grid=7), Y=Speed (0~600, Grid=4)
    blendspace.set_editor_property("blend_parameter_x", unreal.BlendParameter())
    blendspace.set_editor_property("blend_parameter_y", unreal.BlendParameter())

    blendspace.blend_parameter_x.display_name = "Direction"
    blendspace.blend_parameter_x.min = -180.0
    blendspace.blend_parameter_x.max = 180.0
    blendspace.blend_parameter_x.grid_num = 7
    blendspace.blend_parameter_y.display_name = "Speed"
    blendspace.blend_parameter_y.min = 0.0
    blendspace.blend_parameter_y.max = 600.0
    blendspace.blend_parameter_y.grid_num = 4

    blendspace.set_editor_property("axis_to_scale_animation", unreal.BlendSpaceAxis.Y_AXIS)

    # 放置采样点
    placed = 0
    skipped = 0
    for speed, angles in BLENDSPACE_SAMPLES.items():
        for angle, anim_name in angles.items():
            anim = load_anim(anim_name)
            if anim:
                blendspace.add_sample(anim, unreal.Vector(angle, speed, 0))
                placed += 1
                unreal.log(f"  + [{speed}]@{angle} → {anim_name}")
            else:
                skipped += 1
                unreal.log_warning(f"  × Missing: {anim_name}")

    # 保存
    unreal.EditorAssetLibrary.save_asset(bs_path)
    unreal.log(f"BlendSpace created: {placed} samples, {skipped} missing")
    return blendspace


def create_anim_blueprint():
    """创建 ABP_Sekiro"""
    abp_name = "ABP_Sekiro"
    abp_path = f"{OUTPUT_PATH}/{abp_name}"

    # 删除已有资产
    if unreal.EditorAssetLibrary.does_asset_exist(abp_path):
        unreal.EditorAssetLibrary.delete_asset(abp_path)
        unreal.log(f"Deleted existing: {abp_path}")

    # 加载骨架和 AnimInstance 类
    skeleton = load_asset(f"{OUTPUT_PATH}/Sekiro_Skeleton")
    if not skeleton:
        unreal.log_error("Skeleton not found!")
        return None

    # 创建 AnimBlueprint
    anim_bp_factory = unreal.AnimBlueprintFactory()
    anim_bp_factory.set_editor_property("target_skeleton", skeleton)
    # 设置父类为 USekiroAnimInstance
    anim_bp_factory.set_editor_property("parent_class", unreal.load_class(None, "/Script/Sekiro.SKAnimInstance"))

    anim_bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        abp_name, OUTPUT_PATH, unreal.AnimBlueprint, anim_bp_factory
    )
    if not anim_bp:
        unreal.log_error("Failed to create AnimBlueprint!")
        return None

    # 保存
    unreal.EditorAssetLibrary.save_asset(abp_path)
    unreal.log(f"AnimBlueprint created: {abp_path}")
    unreal.log("Manual steps remaining:")
    unreal.log("  1. Open ABP_Sekiro in Persona")
    unreal.log("  2. Set up State Machine: Entry → Locomotion (BS_Sekiro_Locomotion)")
    unreal.log("  3. Add Dodge/Quickstep states (use BlendSpace transitions)")
    unreal.log("  4. Add Crouch state (use Anim_Sekiro_Attack_Crouch if available)")
    unreal.log("  5. Assign ABP_Sekiro to BP_SekiroCharacter's Mesh → Anim Class")
    return anim_bp


def assign_abp_to_character(abp):
    """将 ABP_Sekiro 分配给 BP_SekiroCharacter 的 Mesh → Anim Class"""
    char_path = "/Game/Gameplay/BP_SekiroCharacter"
    try:
        bp_char = load_asset(char_path)
        if not bp_char:
            unreal.log_error(f"BP_SekiroCharacter not found: {char_path}")
            return False

        # 获取生成的 CDO
        cdo = unreal.get_default_object(bp_char.generated_class())
        mesh = cdo.get_editor_property("mesh")
        if not mesh:
            unreal.log_error("BP_SekiroCharacter has no Mesh component!")
            return False

        mesh.set_editor_property("anim_class", abp.generated_class())
        unreal.EditorAssetLibrary.save_asset(char_path)
        unreal.log(f"Assigned {abp.get_name()} → BP_SekiroCharacter.Mesh.AnimClass")
        return True
    except Exception as e:
        unreal.log_error(f"Failed to assign ABP: {e}")
        return False


def main():
    unreal.log("=" * 60)
    unreal.log("Creating Sekiro Animation Blueprint")
    unreal.log("=" * 60)

    # 强制扫描 Asset Registry（Commandlet 模式可能未初始化）
    unreal.log("Scanning Asset Registry...")
    try:
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        ar.search_all_assets(True)
        unreal.log("Asset Registry scan complete")
    except Exception as e:
        unreal.log_warning(f"Asset Registry scan failed (may be OK): {e}")

    bs = create_blendspace()
    abp = create_anim_blueprint()

    if bs and abp:
        assign_abp_to_character(abp)
        unreal.log("=" * 60)
        unreal.log("SUCCESS: BlendSpace + ABP created + assigned!")
        unreal.log("  BS_Sekiro_Locomotion → ready")
        unreal.log("  ABP_Sekiro → BP_SekiroCharacter.Mesh.AnimClass")
        unreal.log("=" * 60)
    else:
        unreal.log_error("FAILED: Some assets could not be created")


if __name__ == "__main__":
    main()
