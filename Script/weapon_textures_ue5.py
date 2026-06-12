"""
Import weapon PNG textures into UE5.
Usage: UE5 Editor -> Window -> Python Console -> paste and run
  or: Tools -> Execute Python Script -> select this file
"""
import unreal
import os

SOURCE_DIR = "F:/ProjectAI/Sekiro/Content/Weapons/Kusabimaru/Textures"
DEST_PATH = "/Game/Weapons/Kusabimaru/Textures"


def main():
    if not os.path.isdir(SOURCE_DIR):
        unreal.log_error("Source dir not found: " + SOURCE_DIR)
        return

    png_files = [f for f in os.listdir(SOURCE_DIR) if f.lower().endswith(".png")]
    if not png_files:
        unreal.log_warning("No PNG files found")
        return

    unreal.log("Importing " + str(len(png_files)) + " textures -> " + DEST_PATH)

    if not unreal.EditorAssetLibrary.does_directory_exist(DEST_PATH):
        unreal.EditorAssetLibrary.make_directory(DEST_PATH)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    ok = 0
    skip = 0
    fail = 0

    for filename in sorted(png_files):
        asset_name = os.path.splitext(filename)[0]
        dest_asset_path = DEST_PATH + "/" + asset_name

        if unreal.EditorAssetLibrary.does_asset_exist(dest_asset_path):
            skip += 1
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
            ok += 1
        else:
            unreal.log_warning("Import failed: " + filename)
            fail += 1

        if ok > 0 and ok % 3 == 0:
            unreal.log("Progress: " + str(ok) + "/" + str(len(png_files)))

    unreal.log("Done: " + str(ok) + " new, " + str(skip) + " exist, " + str(fail) + " fail")


if __name__ == '__main__':
    main()
