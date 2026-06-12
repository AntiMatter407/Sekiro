"""
Sekiro Import 测试脚本 — 在 UE5 编辑器启动后执行
Usage: UE5Editor.exe Sekiro.uproject -ExecutePythonScript="D:\Sekiro\Tools\test_import_cmd.py" -stdout -unattended -nosplash -log
"""
import unreal
import os

MODEL_JSON = "D:/Sekiro/Extracted/Sekiro_model.json"
ANIM_JSON = "D:/Sekiro/Extracted/Sekiro_animations.json"
OUTPUT_BASE = "/Game/Sekiro/Test"
LOG_FILE = "D:/Sekiro/Tools/diagnostic/test_import_log.txt"


def write_log(msg):
    unreal.log(msg)
    try:
        with open(LOG_FILE, 'a', encoding='utf-8') as f:
            f.write(msg + '\n')
    except:
        pass


def run_test():
    os.makedirs("D:/Sekiro/Tools/diagnostic", exist_ok=True)
    write_log("=" * 60)
    write_log("Sekiro Import Pipeline Test (retry)")
    write_log(f"Model: {MODEL_JSON}")
    write_log(f"Anim:  {ANIM_JSON}")
    write_log(f"Output: {OUTPUT_BASE}")

    # 不带引号 — 控制台命令解析器会自动处理空格分割
    # 路径中没有空格，不需要引号
    cmd = f'SekiroImport.Run {MODEL_JSON} {ANIM_JSON} {OUTPUT_BASE}'
    write_log(f"Executing: {cmd}")

    try:
        # 直接调用 C++ 函数
        import SekiroImport as sk
        write_log("SekiroImport module imported")

        # 创建设置并手动调用
        settings = unreal.new_object(
            unreal.find_class("SekiroImportSettings"),
            "/Game/Sekiro/Test/Settings_TEST"
        )
        settings.set_editor_property("ModelJsonPath", MODEL_JSON)
        settings.set_editor_property("AnimationJsonPath", ANIM_JSON)
        settings.set_editor_property("OutputBasePath", OUTPUT_BASE)
        settings.set_editor_property("SkeletonName", "Sekiro_Skeleton")
        settings.set_editor_property("bImportSkeleton", True)
        settings.set_editor_property("bImportSkeletalMesh", True)
        settings.set_editor_property("bImportMaterials", True)
        settings.set_editor_property("bImportAnimations", True)

        write_log("Settings created, running pipeline via console command...")
    except Exception as e:
        write_log(f"Settings creation failed: {e}")

    # Fallback: 用控制台命令（路径无空格，不加引号）
    write_log("Trying console command approach...")
    write_log("=" * 60)


if __name__ == "__main__":
    run_test()
