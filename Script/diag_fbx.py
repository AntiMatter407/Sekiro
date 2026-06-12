"""诊断：导入 ExportedFlver.fbx，打印所有骨骼的世界坐标。"""
import bpy, sys

bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete()

fbx_path = sys.argv[sys.argv.index("--") + 1]
bpy.ops.import_scene.fbx(filepath=fbx_path)

arm = next((o for o in bpy.data.objects if o.type == 'ARMATURE'), None)
if not arm:
    print("NO ARMATURE FOUND"); sys.exit(1)

bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode='EDIT')
for b in arm.data.edit_bones:
    h = b.head
    t = b.tail
    print(f"BONE {b.name}: head=({h.x:.4f},{h.y:.4f},{h.z:.4f}) tail=({t.x:.4f},{t.y:.4f},{t.z:.4f})")
