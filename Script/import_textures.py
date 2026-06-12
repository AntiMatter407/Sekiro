"""
批量导入 Sekiro 贴图到 UE5 Content
用法: UE5编辑器 → Window → Python Console → 粘贴运行
 或: Tools → Execute Python Script → 选择此文件
"""
import unreal
import os

SOURCE_DIR = os.path.join(unreal.Paths.project_dir(), "Extracted", "Textures")
DEST_PATH = "/Game/Characters/Sekiro/Textures"

def import_textures():
    if not os.path.isdir(SOURCE_DIR):
        unreal.log_error(f"源目录不存在: {SOURCE_DIR}")
        return

    png_files = [f for f in os.listdir(SOURCE_DIR) if f.lower().endswith(".png")]
    if not png_files:
        unreal.log_warning("未找到PNG文件")
        return

    unreal.log(f"开始导入 {len(png_files)} 张贴图 → {DEST_PATH}")

    # 确保目标路径存在
    if not unreal.EditorAssetLibrary.does_directory_exist(DEST_PATH):
        unreal.EditorAssetLibrary.make_directory(DEST_PATH)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    imported = 0
    skipped = 0

    for filename in sorted(png_files):
        asset_name = os.path.splitext(filename)[0]
        dest_asset_path = f"{DEST_PATH}/{asset_name}"

        # 跳过已存在
        if unreal.EditorAssetLibrary.does_asset_exist(dest_asset_path):
            skipped += 1
            continue

        source_path = os.path.join(SOURCE_DIR, filename)

        task = unreal.AssetImportTask()
        task.set_editor_property("filename", source_path)
        task.set_editor_property("destination_path", DEST_PATH)
        task.set_editor_property("destination_name", asset_name)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", True)
        task.set_editor_property("replace_existing", False)

        asset_tools.import_asset_tasks([task])

        if task.get_editor_property("imported_object_paths"):
            imported += 1
        else:
            unreal.log_warning(f"导入失败: {filename}")

        if imported % 10 == 0:
            unreal.log(f"进度: {imported}/{len(png_files)}")

    unreal.log(f"导入完成: {imported} 新建, {skipped} 已存在跳过, 共 {len(png_files)}")

import_textures()
