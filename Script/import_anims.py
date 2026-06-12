"""
Import animation FBX: skeleton + animations (no meshes).

Usage:
  blender --background --python import_anims.py -- model.json anim.json output.fbx

Example:
  blender --background --python import_anims.py -- Extracted/Sekiro_model_hkx.json Extracted/Sekiro_common_anims.json Extracted/Sekiro_Anim.fbx

Note: The model.json is required to build the same union skeleton as the model FBX,
so UE5 can link animation curves to the correct skeleton.
"""

import sys
import os
import json

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common_blender import (
    clear_scene, build_armature, add_actions,
    apply_scene_orientation_fix, export_fbx,
)


def main(model_json, anim_json, output_fbx):
    with open(model_json, 'r', encoding='utf-8') as f:
        model_data = json.load(f)
    with open(anim_json, 'r', encoding='utf-8') as f:
        anim_data = json.load(f)

    clear_scene()

    arm_obj, _ = build_armature(model_data, anim_data)
    add_actions(arm_obj, anim_data)
    apply_scene_orientation_fix(arm_obj)

    export_fbx(output_fbx, [arm_obj], bake_anim=True)
    print("Done")


if __name__ == '__main__':
    argv = sys.argv
    if '--' in argv:
        argv = argv[argv.index('--') + 1:]
    if len(argv) < 3:
        print('Usage: blender --background --python import_anims.py -- model.json anim.json output.fbx')
    else:
        main(argv[0], argv[1], argv[2])
