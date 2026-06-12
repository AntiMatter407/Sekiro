"""
Import model FBX: skeleton + meshes + materials (no animations).

Usage:
  blender --background --python import_model.py -- model.json anim.json output.fbx texture_root

Example:
  blender --background --python import_model.py -- Extracted/Sekiro_model_hkx.json Extracted/Sekiro_common_anims.json Extracted/Sekiro_Model.fbx Extracted/
"""

import sys
import os
import json

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common_blender import (
    clear_scene, build_armature, create_materials, build_meshes,
    apply_scene_orientation_fix, export_fbx, _simulate_hair_gravity,
)


def main(model_json, anim_json, output_fbx, texture_root):
    with open(model_json, 'r', encoding='utf-8') as f:
        model_data = json.load(f)
    with open(anim_json, 'r', encoding='utf-8') as f:
        anim_data = json.load(f)

    clear_scene()

    arm_obj, created_bones = build_armature(model_data, anim_data)
    materials = create_materials(model_data.get('Materials', []), texture_root)
    build_meshes(model_data, arm_obj, created_bones, materials)
    _simulate_hair_gravity(arm_obj, model_data)
    apply_scene_orientation_fix(arm_obj)

    mesh_objects = [obj for obj in bpy.data.objects if obj.type == 'MESH']
    export_fbx(output_fbx, [arm_obj] + mesh_objects, bake_anim=False)
    print("Done")


if __name__ == '__main__':
    argv = sys.argv
    if '--' in argv:
        argv = argv[argv.index('--') + 1:]
    if len(argv) < 4:
        print('Usage: blender --background --python import_model.py -- model.json anim.json output.fbx texture_root')
    else:
        main(argv[0], argv[1], argv[2], argv[3])
