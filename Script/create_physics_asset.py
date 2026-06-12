"""
为 Sekiro 角色创建 Physics Asset
使用 PhysicsAssetFactory 自动生成物理体（等同于编辑器右键→Create Physics Asset）
"""
import unreal

SK_MESH_PATH = "/Game/Characters/Sekiro/Sekiro_SkeletalMesh"
PHYSICS_ASSET_PATH = "/Game/Characters/Sekiro"
PHYSICS_ASSET_NAME = "Sekiro_PhysicsAsset"
FULL_PATH = f"{PHYSICS_ASSET_PATH}/{PHYSICS_ASSET_NAME}"
LOG_FILE = "F:/ProjectAI/Sekiro/Script/physics_asset_result.txt"

results = []

def log(msg):
    results.append(msg)
    unreal.log(msg)

def main():
    log("=== 创建 Sekiro Physics Asset ===")

    # 1. 加载骨骼网格
    log(f"加载 SkeletalMesh: {SK_MESH_PATH}")
    sk_mesh = unreal.load_asset(SK_MESH_PATH)
    if not sk_mesh:
        log(f"ERROR: 未找到 {SK_MESH_PATH}")
        return False

    log(f"  SkeletalMesh: {sk_mesh.get_name()}")
    skel = sk_mesh.skeleton
    log(f"  Skeleton: {skel.get_name() if skel else 'None'}")

    # 2. 检查是否已存在
    existing = unreal.load_asset(FULL_PATH)
    if existing:
        log(f"已存在旧 Physics Asset，删除: {FULL_PATH}")
        unreal.EditorAssetLibrary.delete_asset(FULL_PATH)

    # 3. 使用 PhysicsAssetFactory 创建
    log("创建 PhysicsAssetFactory...")
    factory = unreal.PhysicsAssetFactory()
    factory.set_editor_property("target_skeletal_mesh", sk_mesh)
    log(f"  Factory target set to: {sk_mesh.get_name()}")

    # 4. 通过 AssetTools 创建资产
    log("通过 AssetTools 创建 Physics Asset...")
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    physics_asset = asset_tools.create_asset(
        PHYSICS_ASSET_NAME,
        PHYSICS_ASSET_PATH,
        unreal.PhysicsAsset,
        factory
    )

    if not physics_asset:
        log("ERROR: 创建 Physics Asset 失败")
        return False

    log(f"Physics Asset 已创建: {physics_asset.get_name()}")
    log(f"  Path: {physics_asset.get_path_name()}")

    # 5. 保存
    unreal.EditorAssetLibrary.save_asset(FULL_PATH, only_if_is_dirty=False)
    log(f"已保存: {FULL_PATH}")

    # 6. 将 Physics Asset 关联到 SkeletalMesh
    sk_mesh.set_editor_property("physics_asset", physics_asset)
    unreal.EditorAssetLibrary.save_asset(SK_MESH_PATH, only_if_is_dirty=False)
    log("Physics Asset 已关联到 SkeletalMesh")

    log("\n=== 完成 ===")
    return True

if __name__ == "__main__":
    try:
        ok = main()
    except Exception as e:
        log(f"EXCEPTION: {e}")
        ok = False

    with open(LOG_FILE, 'w', encoding='utf-8') as f:
        f.write('\n'.join(results))
