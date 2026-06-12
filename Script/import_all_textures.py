"""
批量导入所有贴图 — 使用 UTextureFactory 绕过 Interchange 死锁
等同于 C++ ImportTextures() 的 Python 版本
"""
import unreal
import os
import time

SOURCE = unreal.Paths.project_dir() + "Extracted/Textures"
DEST = "/Game/Characters/Sekiro/Textures"

if not os.path.isdir(SOURCE):
    unreal.log_error(f"源目录不存在: {SOURCE}")
    raise SystemExit

pngs = sorted([f for f in os.listdir(SOURCE) if f.lower().endswith(".png")])
total = len(pngs)
if not total:
    unreal.log_error(f"无PNG文件: {SOURCE}")
    raise SystemExit

unreal.log(f"=== 开始批量导入 {total} 张贴图 ===")

# 确保目录存在
if not unreal.EditorAssetLibrary.does_directory_exist(DEST):
    unreal.EditorAssetLibrary.make_directory(DEST)

at = unreal.AssetToolsHelpers.get_asset_tools()
imported = 0
skipped = 0
start_time = time.time()

for i, filename in enumerate(pngs):
    asset_name = os.path.splitext(filename)[0]
    asset_path = f"{DEST}/{asset_name}"

    # 跳过已存在
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        skipped += 1
        continue

    # 创建 UTextureFactory 绕过 Interchange（避免 WaitUntilDone GameThread死锁）
    tex_factory = unreal.TextureFactory()
    tex_factory.set_editor_property("defer_compression", True)

    import_data = unreal.AutomatedAssetImportData()
    import_data.set_editor_property("filenames", [os.path.join(SOURCE, filename)])
    import_data.set_editor_property("destination_path", DEST)
    import_data.set_editor_property("replace_existing", False)
    import_data.set_editor_property("skip_read_only", True)
    import_data.set_editor_property("factory", tex_factory)

    result = at.import_assets_automated(import_data)

    if result:
        pkgs = []
        for obj in result:
            if obj:
                pkgs.append(obj.get_outermost())
        if pkgs:
            unreal.EditorLoadingAndSavingUtils.save_packages(pkgs, True)
        imported += 1
    else:
        unreal.log_warning(f"  导入失败: {filename}")

    if (i + 1) % 10 == 0 or (i + 1) == total:
        elapsed = time.time() - start_time
        unreal.log(f"  进度: {i+1}/{total} (导入{imported}, 跳过{skipped}) 耗时{elapsed:.1f}s")

elapsed = time.time() - start_time
unreal.log(f"=== 导入完成: {imported} 新建, {skipped} 跳过, 共 {total} 贴图, 耗时 {elapsed:.1f}s ===")
