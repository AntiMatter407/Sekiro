"""
Convert all DDS files under a root directory to PNG.
Usage:
  blender --background --python convert_dds_to_png.py -- <texture_root>
"""
import bpy
import sys
import os


def convert_all(texture_root):
    converted = 0
    skipped = 0
    failed = 0
    for root, dirs, files in os.walk(texture_root):
        for f in files:
            if not f.lower().endswith('.dds'):
                continue
            dds_path = os.path.join(root, f)
            png_path = os.path.splitext(dds_path)[0] + '.png'
            if os.path.exists(png_path):
                skipped += 1
                continue
            try:
                img = bpy.data.images.load(dds_path)
                img.file_format = 'PNG'
                img.filepath_raw = png_path
                img.save()
                bpy.data.images.remove(img)
                converted += 1
                print(f"  OK: {f} -> {os.path.basename(png_path)}")
            except Exception as e:
                failed += 1
                print(f"  FAIL: {f}: {e}")
    print(f"\nDone. Converted={converted}, Skipped(already exist)={skipped}, Failed={failed}")


if __name__ == '__main__':
    argv = sys.argv
    if '--' in argv:
        argv = argv[argv.index('--') + 1:]
    if not argv:
        print("Usage: blender --background --python convert_dds_to_png.py -- <texture_root>")
        sys.exit(1)
    convert_all(argv[0])
