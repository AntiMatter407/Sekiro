"""
测试导入单张贴图 — 验证 ImportAssetsAutomated + UTextureFactory 绕过 Interchange 死锁
"""
import unreal
import os

SOURCE = unreal.Paths.project_dir() + "Extracted/Textures"
DEST = "/Game/Characters/Sekiro/Textures"

if not os.path.isdir(SOURCE):
    unreal.log_error(f"源目录不存在: {SOURCE}")
    raise SystemExit

pngs = sorted([f for f in os.listdir(SOURCE) if f.lower().endswith(".png")])
if not pngs:
    unreal.log_error(f"无PNG文件: {SOURCE}")
    raise SystemExit

# 取第一个文件做测试
test_file = pngs[0]
size_kb = os.path.getsize(os.path.join(SOURCE, test_file)) / 1024
unreal.log(f"测试文件: {test_file} ({size_kb:.1f} KB)")

asset_name = os.path.splitext(test_file)[0]
asset_path = f"{DEST}/{asset_name}"

# 跳过已存在
if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
    unreal.log(f"资产已存在，跳过: {asset_path}")
    raise SystemExit

# 确保目录存在
if not unreal.EditorAssetLibrary.does_directory_exist(DEST):
    unreal.EditorAssetLibrary.make_directory(DEST)

at = unreal.AssetToolsHelpers.get_asset_tools()

# 创建 UTextureFactory 绕过 Interchange（避免 WaitUntilDone GameThread死锁）
tex_factory = unreal.TextureFactory()
tex_factory.set_editor_property("defer_compression", True)

import_data = unreal.AutomatedAssetImportData()
import_data.set_editor_property("filenames", [os.path.join(SOURCE, test_file)])
import_data.set_editor_property("destination_path", DEST)
import_data.set_editor_property("replace_existing", False)
import_data.set_editor_property("skip_read_only", True)
import_data.set_editor_property("factory", tex_factory)  # 关键：指定工厂绕过Interchange

unreal.log("开始调用 ImportAssetsAutomated (with UTextureFactory)...")
result = at.import_assets_automated(import_data)
unreal.log(f"ImportAssetsAutomated 返回: {len(result)} 个对象")

if result:
    pkgs = []
    for obj in result:
        if obj:
            pkgs.append(obj.get_outermost())
            unreal.log(f"  导入: {obj.get_name()} (包: {obj.get_outermost().get_name()})")

    if pkgs:
        unreal.EditorLoadingAndSavingUtils.save_packages(pkgs, True)
        unreal.log(f"保存完成: {len(pkgs)} 个包")

    asset_exists = unreal.EditorAssetLibrary.does_asset_exist(asset_path)
    unreal.log(f"资产存在验证: {asset_exists} -> {asset_path}")
else:
    unreal.log_error(f"导入失败: {test_file}")
