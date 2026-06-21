"""
CLI —— SekiroAssetManager 命令行入口。

支持以下命令格式：

    # 配置文件查看
    python -m sekiro_asset_manager config show

    # 环境检查
    python -m sekiro_asset_manager check

    # 独立导入
    python -m sekiro_asset_manager model import <asset_name> [--parts ...]
    python -m sekiro_asset_manager anim import <asset_name> [--anibnd-dir ...]
    python -m sekiro_asset_manager material import <asset_name> [--texture-dir ...]

    # 全自动导入
    python -m sekiro_asset_manager all import <asset_name>

    # 批量动画导入
    python -m sekiro_asset_manager anim import-all <base_asset>

    # 信息查询
    python -m sekiro_asset_manager info <asset_name>

所有命令支持 --help 查看完整参数说明。
"""

import argparse
import sys
import json

from sekiro_asset_manager.manager import SekiroAssetManager


# ======================================================================
# 子命令构建器
# ======================================================================

def _add_model_subcommand(subparsers) -> None:
    """添加 model import 子命令。

    Args:
        subparsers: argparse 的子命令解析器。
    """
    parser = subparsers.add_parser("model", help="模型导入相关命令")
    model_sub = parser.add_subparsers(dest="action", required=True)

    # model import
    import_cmd = model_sub.add_parser("import", help="导入模型（骨架 + 网格体）")
    import_cmd.add_argument("asset_name", help="资产名，如 WP_A_0300_Kusabimaru")
    import_cmd.add_argument("--parts", nargs="+", help="部件 FLVER 路径列表")
    import_cmd.add_argument("--skeleton-flver", help="骨架 FLVER 路径")
    import_cmd.add_argument("--skeleton-hkx", help="骨架 HKX 路径")
    import_cmd.add_argument("--output-json", help="FlverToJson 输出的 JSON 路径")
    import_cmd.add_argument("--output-fbx", help="Blender 导出的 FBX 路径")
    import_cmd.add_argument("--target-ue-path", help="UE 内容浏览器中的目标路径")
    import_cmd.add_argument("--skip-unpack", action="store_true", help="跳过解包步骤")
    import_cmd.add_argument("--skip-flver-to-fbx", action="store_true", help="跳过 FlverToJson 步骤")
    import_cmd.add_argument("--skip-blender", action="store_true", help="跳过 Blender 步骤")
    import_cmd.add_argument("--skip-ue-import", action="store_true", help="跳过 UE 导入步骤")


def _add_anim_subcommand(subparsers) -> None:
    """添加 anim / animation import 和 import-all 子命令。

    Args:
        subparsers: argparse 的子命令解析器。
    """
    parser = subparsers.add_parser(
        "anim", aliases=["animation"], help="动画导入相关命令"
    )
    anim_sub = parser.add_subparsers(dest="action", required=True)

    # anim import
    import_cmd = anim_sub.add_parser("import", help="导入动画")
    import_cmd.add_argument("asset_name", help="资产名，如 c0000 或 c0000_a000_hi")
    import_cmd.add_argument("--anibnd-dir", help="anibnd 解包目录路径")
    import_cmd.add_argument("--model-json", help="模型 JSON 路径（含骨架定义）")
    import_cmd.add_argument("--anim-json", help="动画 JSON 路径（提取器输出）")
    import_cmd.add_argument("--output-fbx", help="Blender 导出的 FBX 路径")
    import_cmd.add_argument("--target-ue-path", help="UE 动画导入路径")
    import_cmd.add_argument("--skip-unpack", action="store_true", help="跳过 anibnd 解包")
    import_cmd.add_argument("--skip-extractor", action="store_true", help="跳过动画提取器")
    import_cmd.add_argument("--skip-blender", action="store_true", help="跳过 Blender 导出")
    import_cmd.add_argument("--skip-ue-import", action="store_true", help="跳过 UE 导入")

    # anim import-all (批量)
    import_all_cmd = anim_sub.add_parser(
        "import-all", help="批量导入角色的所有 anibnd 包"
    )
    import_all_cmd.add_argument(
        "base_asset", nargs="?", default="c0000",
        help="基础资产代码，默认 c0000",
    )
    import_all_cmd.add_argument("--model-json", help="模型 JSON 路径")
    import_all_cmd.add_argument("--ue-root", help="UE 导入根路径")
    import_all_cmd.add_argument(
        "--skip-main", action="store_true",
        help="跳过主 anibnd 包（只导入子包）",
    )
    import_all_cmd.add_argument("--skip-unpack", action="store_true", help="跳过解包")
    import_all_cmd.add_argument("--skip-extractor", action="store_true", help="跳过提取器")
    import_all_cmd.add_argument("--skip-blender", action="store_true", help="跳过 Blender")
    import_all_cmd.add_argument("--skip-ue-import", action="store_true", help="跳过 UE 导入")


def _add_material_subcommand(subparsers) -> None:
    """添加 material import 子命令。

    Args:
        subparsers: argparse 的子命令解析器。
    """
    parser = subparsers.add_parser("material", help="材质导入相关命令")
    mat_sub = parser.add_subparsers(dest="action", required=True)

    # material import
    import_cmd = mat_sub.add_parser("import", help="导入材质和纹理")
    import_cmd.add_argument("asset_name", help="资产名（仅用于日志）")
    import_cmd.add_argument("--model-json", help="模型 JSON 路径（含 ResolvedMaterials）")
    import_cmd.add_argument("--target-ue-path", help="UE 中的目标根路径")
    import_cmd.add_argument("--mesh-path", help="UE 中 SkeletalMesh 路径（用于材质分配）")
    import_cmd.add_argument("--texture-dir", help="本地纹理目录")
    import_cmd.add_argument("--texture-ue-path", help="UE 纹理导入路径")
    import_cmd.add_argument("--material-ue-dir", help="UE 材质存放路径")
    import_cmd.add_argument(
        "--material-mode", choices=["material", "instance"], default="instance",
        help="材质模式：material（独立 UMaterial）或 instance（MI，默认）",
    )
    import_cmd.add_argument("--skip-texconv", action="store_true", help="跳过 DDS→PNG 转换")
    import_cmd.add_argument("--skip-ue-texture", action="store_true", help="跳过 UE 纹理导入")
    import_cmd.add_argument("--skip-material", action="store_true", help="跳过材质创建")
    import_cmd.add_argument("--skip-mesh-assign", action="store_true", help="跳过网格材质分配")


def _add_all_subcommand(subparsers) -> None:
    """添加 all import 全自动管线子命令。

    Args:
        subparsers: argparse 的子命令解析器。
    """
    parser = subparsers.add_parser("all", help="全自动导入管线")
    all_sub = parser.add_subparsers(dest="action", required=True)

    # all import
    import_cmd = all_sub.add_parser("import", help="全自动导入模型 + 动画 + 材质")
    import_cmd.add_argument("asset_name", help="资产名")
    import_cmd.add_argument("--model-json", help="模型 JSON 路径（手动指定）")
    import_cmd.add_argument(
        "--skip-model", action="store_true", help="跳过模型导入步骤"
    )


def _add_config_subcommand(subparsers) -> None:
    """添加 config show 和 config check 子命令。

    Args:
        subparsers: argparse 的子命令解析器。
    """
    parser = subparsers.add_parser("config", help="配置管理命令")
    config_sub = parser.add_subparsers(dest="action", required=True)

    config_sub.add_parser("show", help="显示当前配置和路径")
    config_sub.add_parser("check", aliases=["verify"], help="验证前置条件是否满足")


def _add_info_subcommand(subparsers) -> None:
    """添加 info 子命令，用于查询资产摘要。

    Args:
        subparsers: argparse 的子命令解析器。
    """
    info_parser = subparsers.add_parser(
        "info", help="查看资产摘要信息"
    )
    info_parser.add_argument("asset_name", help="资产名")
    info_parser.add_argument("--json", action="store_true", help="以 JSON 格式输出")


# ======================================================================
# 子命令执行
# ======================================================================

def _execute_model_import(args: argparse.Namespace, mgr: SekiroAssetManager) -> dict:
    """执行 model import 子命令。

    Args:
        args: 命令行解析后的参数。
        mgr: SekiroAssetManager 实例。

    Returns:
        dict: 导入结果。
    """
    kwargs = {}
    if args.parts:
        kwargs["flver_paths"] = args.parts
    if args.skeleton_flver:
        kwargs["skeleton_flver"] = args.skeleton_flver
    if args.skeleton_hkx:
        kwargs["skeleton_hkx"] = args.skeleton_hkx
    if args.output_json:
        kwargs["output_json"] = args.output_json
    if args.output_fbx:
        kwargs["output_fbx"] = args.output_fbx
    if args.target_ue_path:
        kwargs["target_ue_path"] = args.target_ue_path
    if args.skip_unpack:
        kwargs["skip_unpack"] = True
    if args.skip_flver_to_fbx:
        kwargs["skip_flver_to_fbx"] = True
    if args.skip_blender:
        kwargs["skip_blender"] = True
    if args.skip_ue_import:
        kwargs["skip_ue_import"] = True

    return mgr.import_model(args.asset_name, **kwargs)


def _execute_anim_import(args: argparse.Namespace, mgr: SekiroAssetManager) -> dict:
    """执行 anim import 子命令。

    Args:
        args: 命令行解析后的参数。
        mgr: SekiroAssetManager 实例。

    Returns:
        dict: 导入结果。
    """
    kwargs = {}
    if args.anibnd_dir:
        kwargs["anibnd_dir"] = args.anibnd_dir
    if args.model_json:
        kwargs["model_json"] = args.model_json
    if args.anim_json:
        kwargs["anim_json"] = args.anim_json
    if args.output_fbx:
        kwargs["output_fbx"] = args.output_fbx
    if args.target_ue_path:
        kwargs["target_ue_path"] = args.target_ue_path
    if args.skip_unpack:
        kwargs["skip_unpack"] = True
    if args.skip_extractor:
        kwargs["skip_extractor"] = True
    if args.skip_blender:
        kwargs["skip_blender"] = True
    if args.skip_ue_import:
        kwargs["skip_ue_import"] = True

    return mgr.import_animation(args.asset_name, **kwargs)


def _execute_anim_import_all(
    args: argparse.Namespace, mgr: SekiroAssetManager
) -> dict:
    """执行 anim import-all 子命令。

    Args:
        args: 命令行解析后的参数。
        mgr: SekiroAssetManager 实例。

    Returns:
        dict: 批量导入结果。
    """
    kwargs = {}
    if args.model_json:
        kwargs["model_json"] = args.model_json
    if args.ue_root:
        kwargs["ue_root"] = args.ue_root
    if args.skip_main:
        kwargs["include_main"] = False
    if args.skip_unpack:
        kwargs["skip_unpack"] = True
    if args.skip_extractor:
        kwargs["skip_extractor"] = True
    if args.skip_blender:
        kwargs["skip_blender"] = True
    if args.skip_ue_import:
        kwargs["skip_ue_import"] = True

    return mgr.import_all_anibnd(args.base_asset, **kwargs)


def _execute_material_import(
    args: argparse.Namespace, mgr: SekiroAssetManager
) -> dict:
    """执行 material import 子命令。

    Args:
        args: 命令行解析后的参数。
        mgr: SekiroAssetManager 实例。

    Returns:
        dict: 导入结果。
    """
    kwargs = {}
    if args.model_json:
        kwargs["model_json"] = args.model_json
    if args.target_ue_path:
        kwargs["target_ue_path"] = args.target_ue_path
    if args.mesh_path:
        kwargs["mesh_path"] = args.mesh_path
    if args.texture_dir:
        kwargs["texture_dir"] = args.texture_dir
    if args.texture_ue_path:
        kwargs["texture_ue_path"] = args.texture_ue_path
    if args.material_ue_dir:
        kwargs["material_ue_dir"] = args.material_ue_dir
    if args.material_mode:
        kwargs["material_mode"] = args.material_mode
    if args.skip_texconv:
        kwargs["skip_texconv"] = True
    if args.skip_ue_texture:
        kwargs["skip_ue_texture_import"] = True
    if args.skip_material:
        kwargs["skip_material_create"] = True
    if args.skip_mesh_assign:
        kwargs["skip_mesh_assign"] = True

    return mgr.import_material(args.asset_name, **kwargs)


def _execute_all_import(args: argparse.Namespace, mgr: SekiroAssetManager) -> dict:
    """执行 all import 全自动管线子命令。

    Args:
        args: 命令行解析后的参数。
        mgr: SekiroAssetManager 实例。

    Returns:
        dict: 全自动导入结果。
    """
    model_kwargs = {}
    anim_kwargs = {}
    mat_kwargs = {}

    # 如果指定了 model-json，传递给所有需要它的步骤
    if args.model_json:
        model_kwargs["output_json"] = args.model_json
        anim_kwargs["model_json"] = args.model_json
        mat_kwargs["model_json"] = args.model_json

    if args.skip_model:
        # 仅导入动画和材质
        anim_result = mgr.import_animation(args.asset_name, **anim_kwargs)
        mat_result = mgr.import_material(args.asset_name, **mat_kwargs)
        return {
            "success": anim_result.get("success") and mat_result.get("success"),
            "asset_name": args.asset_name,
            "model": {"success": True, "note": "跳过模型导入"},
            "animation": anim_result,
            "material": mat_result,
        }

    return mgr.import_all(
        args.asset_name,
        model_kwargs=model_kwargs,
        anim_kwargs=anim_kwargs,
        mat_kwargs=mat_kwargs,
    )


def _execute_config_show(mgr: SekiroAssetManager) -> None:
    """显示当前配置信息。

    Args:
        mgr: SekiroAssetManager 实例。
    """
    config = mgr.config
    print("=" * 60)
    print("SekiroAssetManager 配置信息")
    print("=" * 60)
    print(f"项目目录:       {config.project_dir}")
    print(f"游戏目录:        {config.game_dir}")
    print(f"引擎目录:        {config.engine_dir}")
    print(f"工具根目录:      {config.tools_dir}")
    print(f"解包输出目录:    {config.extracted_dir}")
    print(f"Content 目录:    {config.content_dir}")
    print(f"脚本目录:        {config.scripts_dir}")
    print()
    print(f"FlverToJson:           {config.tool_path('flver_to_fbx')}")
    print(f"SekiroAnimExtractor:  {config.tool_path('sekiro_anim_extractor')}")
    print(f"Yabber:               {config.tool_path('yabber')}")
    print(f"texconv:              {config.tool_path('texconv')}")
    print()
    print(f"Blender:              {config.get_blender()}")
    print(f"UE Python:            {config.get_python()}")
    print(f"bridge.py:            {config.get_bridge_py()}")


def _execute_config_check(mgr: SekiroAssetManager) -> None:
    """执行前置条件验证并展示检查结果。

    Args:
        mgr: SekiroAssetManager 实例。
    """
    checks = mgr.verify_prerequisites()

    labels = {
        "game_dir": "游戏目录",
        "engine_dir": "引擎目录",
        "flver_to_fbx": "FlverToJson",
        "sekiro_anim_extractor": "SekiroAnimExtractor",
        "yabber": "Yabber",
        "texconv": "texconv",
        "blender": "Blender",
        "ue_python": "UE Python",
        "uproject": ".uproject 文件",
    }

    print("=" * 60)
    print("前置条件检查")
    print("=" * 60)

    all_ok = True
    for key, label in labels.items():
        ok = checks.get(key, False)
        status = "OK" if ok else "FAIL"
        if not ok:
            all_ok = False
        print(f"  [{status}] {label}")

    print()
    if all_ok:
        print("所有前置条件满足，可以开始导入。")
    else:
        print("部分前置条件未满足，请检查后重试。")
    print("=" * 60)


def _execute_info(args: argparse.Namespace, mgr: SekiroAssetManager) -> None:
    """显示资产摘要信息。

    Args:
        args: 命令行解析后的参数。
        mgr: SekiroAssetManager 实例。
    """
    summary = mgr.get_summary(args.asset_name)

    if args.json:
        print(json.dumps(summary, indent=2, ensure_ascii=False))
        return

    model = summary.get("model", "")
    anim = summary.get("animation", {})
    mat = summary.get("material", {})

    print(f"资产: {args.asset_name}")
    print(f"  骨架名称:     {model}")

    print(f"  动画:")
    if anim.get("exists"):
        print(f"    动画数:   {anim.get('animation_count', 0)}")
        print(f"    骨骼数:   {anim.get('bone_count', 0)}")
        cats = anim.get("categories", {})
        if cats:
            print(f"    类别:     {', '.join(f'{k}({v})' for k, v in sorted(cats.items()))}")
    else:
        print(f"    动画文件不存在")

    print(f"  材质:")
    if mat.get("exists"):
        print(f"    材质数:         {mat.get('material_count', 0)}")
        print(f"    唯一纹理数:     {mat.get('total_unique_textures', 0)}")
        print(f"    Resolved:       {'是' if mat.get('has_resolved_materials') else '否'}")
    else:
        print(f"    模型文件不存在")


# ======================================================================
# 主入口
# ======================================================================

def main(argv: list[str] = None) -> int:
    """CLI 主入口。

    Args:
        argv: 命令行参数列表。为 None 时从 sys.argv 读取。

    Returns:
        int: 退出码（0 成功，1 失败）。
    """
    parser = argparse.ArgumentParser(
        description="Sekiro 资产导入管理器",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "示例:\n"
            "  python -m sekiro_asset_manager config show\n"
            "  python -m sekiro_asset_manager model import WP_A_0300_Kusabimaru\n"
            "  python -m sekiro_asset_manager anim import c0000_a000_hi\n"
            "  python -m sekiro_asset_manager all import c0000\n"
            "  python -m sekiro_asset_manager anim import-all c0000\n"
        ),
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # 注册所有子命令
    _add_model_subcommand(subparsers)
    _add_anim_subcommand(subparsers)
    _add_material_subcommand(subparsers)
    _add_all_subcommand(subparsers)
    _add_config_subcommand(subparsers)
    _add_info_subcommand(subparsers)

    args = parser.parse_args(argv)

    # ── 创建管理器实例 ──────────────────────────────────────────────────
    mgr = SekiroAssetManager()

    # ── 按命令类型分发 ──────────────────────────────────────────────────
    try:
        if args.command == "config":
            if args.action == "show":
                _execute_config_show(mgr)
                return 0
            elif args.action in ("check", "verify"):
                _execute_config_check(mgr)
                return 0
            else:
                print(f"未知 config 命令: {args.action}")
                return 1

        elif args.command == "info":
            _execute_info(args, mgr)
            return 0

        # 以下命令需要 action 为 "import" 等
        if args.command == "model":
            if args.action == "import":
                result = _execute_model_import(args, mgr)
            else:
                parser.print_help()
                return 1

        elif args.command in ("anim", "animation"):
            if args.action == "import":
                result = _execute_anim_import(args, mgr)
            elif args.action == "import-all":
                result = _execute_anim_import_all(args, mgr)
            else:
                parser.print_help()
                return 1

        elif args.command == "material":
            if args.action == "import":
                result = _execute_material_import(args, mgr)
            else:
                parser.print_help()
                return 1

        elif args.command == "all":
            if args.action == "import":
                result = _execute_all_import(args, mgr)
            else:
                parser.print_help()
                return 1

        else:
            parser.print_help()
            return 1

    except Exception as e:
        print(f"\n[错误] 未处理的异常: {e}")
        import traceback
        traceback.print_exc()
        return 1

    # ── 输出结果 ────────────────────────────────────────────────────────
    success = result.get("success", False)

    if success:
        print(f"\n[成功] {args.command} {args.action} '{args.asset_name}' 完成")
    else:
        error = result.get("error", "未知错误")
        print(f"\n[失败] {args.command} {args.action} '{args.asset_name}': {error}")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())