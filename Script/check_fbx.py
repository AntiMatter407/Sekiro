import bpy, sys
path = sys.argv[sys.argv.index('--') + 1]
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete()
bpy.ops.import_scene.fbx(filepath=path)
print('ACTIONS', len(bpy.data.actions), [a.name for a in bpy.data.actions])
print('ARMATURES', [o.name for o in bpy.data.objects if o.type == 'ARMATURE'])
for o in bpy.data.objects:
    if o.type == 'ARMATURE':
        print('ARM', o.name, 'bones', len(o.data.bones), 'rot', tuple(round(x,3) for x in o.rotation_euler))
print('MESHES', len([o for o in bpy.data.objects if o.type == 'MESH']))
