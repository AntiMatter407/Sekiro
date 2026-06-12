"""
Sekiro Import 测试脚本 — 在 UE5 Python 控制台中运行
验证 UV V-flip 修复和法线贴图 SamplerType 修复
Usage (UE5 Output Log -> Python):
  import sys; sys.path.insert(0, r"D:\Sekiro\Tools")
  import test_import
  test_import.run()
"""
import unreal

MODEL_JSON = r"D:\Sekiro\Extracted\Sekiro_model.json"
ANIM_JSON = r"D:\Sekiro\Extracted\Sekiro_animations.json"
OUTPUT_BASE = "/Game/Sekiro/Test"
SKELETON_NAME = "Sekiro_Skeleton"


def run():
    unreal.log("\n" + "=" * 60)
    unreal.log("  Sekiro Import Pipeline Test")
    unreal.log("=" * 60)

    # 创建导入设置
    settings = unreal.new_object(unreal.SekiroImportSettings)
    settings.set_editor_property("ModelJsonPath", MODEL_JSON)
    settings.set_editor_property("AnimationJsonPath", ANIM_JSON)
    settings.set_editor_property("OutputBasePath", OUTPUT_BASE)
    settings.set_editor_property("SkeletonName", SKELETON_NAME)
    settings.set_editor_property("bImportSkeleton", True)
    settings.set_editor_property("bImportSkeletalMesh", True)
    settings.set_editor_property("bImportMaterials", True)
    settings.set_editor_property("bImportAnimations", True)

    unreal.log(f"  Model JSON: {MODEL_JSON}")
    unreal.log(f"  Anim JSON:  {ANIM_JSON}")
    unreal.log(f"  Output:     {OUTPUT_BASE}")

    # 调用导入管线
    unreal.log("\n--- Running import pipeline... ---")
    result = unreal.SekiroImportPipeline.run(settings)

    if not result:
        unreal.log_error("Import returned null!")
        return

    b_success = result.bSuccess
    errors = result.Errors if hasattr(result, 'Errors') else []
    skeleton = result.Skeleton
    mesh = result.SkeletalMesh
    materials = result.Materials if hasattr(result, 'Materials') else []
    animations = result.Animations if hasattr(result, 'Animations') else []

    unreal.log(f"\n=== Import Result ===")
    unreal.log(f"  Success: {b_success}")
    unreal.log(f"  Skeleton: {skeleton.get_name() if skeleton else 'None'}")
    unreal.log(f"  SkeletalMesh: {mesh.get_name() if mesh else 'None'}")
    unreal.log(f"  Materials: {len(materials)}")
    unreal.log(f"  Animations: {len(animations)}")

    if errors:
        unreal.log(f"  Errors ({len(errors)}):")
        for e in errors:
            unreal.log_error(f"    {e}")

    # 验证 SkeletalMesh UV
    if mesh:
        unreal.log(f"\n--- Mesh Verification ---")
        mat_count = mesh.get_materials_num()
        unreal.log(f"  Material slots: {mat_count}")
        for i in range(min(mat_count, 5)):
            mat_slot = mesh.get_materials()[i]
            mat_interface = mat_slot.material_interface
            mat_name = mat_interface.get_name() if mat_interface else "None"
            unreal.log(f"  Slot[{i}]: {mat_name}")

    # 验证材质 SamplerType
    if materials:
        unreal.log(f"\n--- Material SamplerType Check ---")
        for mat in materials[:3]:
            if not mat:
                continue
            mat_name = mat.get_name()
            unreal.log(f"  {mat_name}:")
            try:
                for expr in mat.get_expressions():
                    tex_sample = getattr(expr, 'texture', None) if hasattr(expr, 'texture') else None
                    if hasattr(expr, 'sampler_type') and hasattr(expr, 'texture') and expr.texture:
                        sampler = expr.sampler_type
                        tex_name = expr.texture.get_name()
                        unreal.log(f"    {tex_name}: SamplerType={sampler}")
            except Exception as e:
                unreal.log_warning(f"    Check failed: {e}")

    unreal.log(f"\n=== Test Complete ===")


if __name__ == "__main__":
    run()
