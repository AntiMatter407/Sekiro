"""
Extract MTD material definition files from Sekiro game data.

MTD files define material properties: blend mode, lighting type, texture slots
(g_DiffuseTexture, g_SpecularTexture, etc.), and shader parameters (metallic,
roughness, etc.). They are the single source of truth for material configuration.

Usage:
    python extract_mtd.py <game_data_dir> [--output Extracted/mtd]

Typical game data layout:
    Sekiro/Data/
      Data1.mtdbnd       # or Data1.mtdbnd.dcx
      Data2.mtdbnd
      ...

Yabber extracts each .mtdbnd into a directory with individual .mtd files.
This script automates the process and copies results to Extracted/mtd/.

Without MTD files, the pipeline falls back to:
    - ParamName heuristic (from FLVER textures)
    - CLOTH_KEYWORDS string matching (for blend mode)
    - Score-based texture assignment
"""

import os
import sys
import subprocess
import shutil
import glob as _glob


def find_yabber():
    """Locate Yabber.exe relative to this script or in Tools/."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(script_dir, '..', '..', '..', 'Tools', 'Yabber 1.3.1', 'Yabber.exe'),
        os.path.join(script_dir, '..', '..', 'Tools', 'Yabber 1.3.1', 'Yabber.exe'),
        os.path.join(script_dir, '..', 'Yabber 1.3.1', 'Yabber.exe'),
    ]
    for c in candidates:
        norm = os.path.normpath(c)
        if os.path.exists(norm):
            return norm
    return None


def find_mtd_bundles(game_data_dir):
    """Find all .mtdbnd and .mtdbnd.dcx files under game_data_dir."""
    bundles = []
    for root, dirs, files in os.walk(game_data_dir):
        for f in files:
            low = f.lower()
            if low.endswith('.mtdbnd') or low.endswith('.mtdbnd.dcx'):
                bundles.append(os.path.join(root, f))
    return sorted(bundles)


def extract_bundle(yabber_path, bundle_path, output_dir):
    """Run Yabber on a single bundle, extract to output_dir."""
    bundle_name = os.path.splitext(os.path.basename(bundle_path))[0]
    # Handle .mtdbnd.dcx double extension
    if bundle_name.lower().endswith('.mtdbnd'):
        bundle_name = os.path.splitext(bundle_name)[0]

    dest = os.path.join(output_dir, bundle_name)
    if os.path.exists(dest):
        print(f"  SKIP (exists): {bundle_name}")
        return dest

    print(f"  Extracting: {os.path.basename(bundle_path)} ...")
    try:
        subprocess.run(
            [yabber_path, bundle_path],
            cwd=output_dir,
            check=True,
            capture_output=True,
            timeout=120,
        )
    except subprocess.CalledProcessError as e:
        print(f"  ERROR: Yabber failed: {e.stderr.decode('utf-8', errors='replace')[:200]}")
        return None

    # Yabber creates a sibling directory next to the input file by default.
    # Look for it and move to output_dir.
    src_dir = bundle_path
    if src_dir.lower().endswith('.dcx'):
        src_dir = src_dir[:-4]
    src_dir = src_dir + '-mtdbnd-dcx' if bundle_path.lower().endswith('.dcx') else src_dir + '-mtdbnd'
    # Also try without -dcx suffix
    if not os.path.exists(src_dir):
        src_dir = os.path.splitext(bundle_path)[0] + '-mtdbnd'

    if os.path.isdir(src_dir):
        shutil.move(src_dir, dest)
        print(f"    -> {dest}")

    return dest


def collect_mtd_files(extracted_dirs, output_root):
    """Copy all .mtd files from extracted directories into a flat layout
    that matches the game's N:\NTC\data\Material\mtd\... structure."""
    mtd_dir = os.path.join(output_root, 'mtd')
    os.makedirs(mtd_dir, exist_ok=True)

    count = 0
    for src_dir in extracted_dirs:
        if not src_dir or not os.path.isdir(src_dir):
            continue
        for root, dirs, files in os.walk(src_dir):
            for f in files:
                if f.lower().endswith('.mtd'):
                    src = os.path.join(root, f)
                    # Preserve relative path structure under mtd/
                    rel = os.path.relpath(root, src_dir)
                    dst_dir = os.path.join(mtd_dir, rel)
                    os.makedirs(dst_dir, exist_ok=True)
                    dst = os.path.join(dst_dir, f)
                    if not os.path.exists(dst):
                        shutil.copy2(src, dst)
                    count += 1

    return count


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        print("ERROR: game_data_dir is required.")
        print("Example: python extract_mtd.py D:/Sekiro/Data")
        sys.exit(1)

    game_data_dir = sys.argv[1]
    output_root = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Extracted')

    yabber = find_yabber()
    if not yabber:
        print("ERROR: Yabber.exe not found. Make sure it's in Tools/Yabber 1.3.1/")
        sys.exit(1)
    print(f"Yabber: {yabber}")

    bundles = find_mtd_bundles(game_data_dir)
    if not bundles:
        print(f"ERROR: No .mtdbnd files found under {game_data_dir}")
        print("MTD bundles are typically in: <Sekiro>/Data/Data1.mtdbnd, etc.")
        sys.exit(1)
    print(f"Found {len(bundles)} MTD bundle(s)")

    work_dir = os.path.join(output_root, '_mtd_extract')
    os.makedirs(work_dir, exist_ok=True)

    extracted = []
    for b in bundles:
        result = extract_bundle(yabber, b, work_dir)
        if result:
            extracted.append(result)

    if not extracted:
        print("ERROR: No bundles extracted successfully.")
        sys.exit(1)

    count = collect_mtd_files(extracted, output_root)
    print(f"\nDone: {count} .mtd files collected to {os.path.join(output_root, 'mtd')}")

    # Cleanup work dir
    shutil.rmtree(work_dir, ignore_errors=True)
    print("Work directory cleaned.")


if __name__ == '__main__':
    main()
