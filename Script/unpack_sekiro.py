"""
Batch unpack Sekiro assets using Yabber.
Unpacks character parts, animations, and textures.

Usage: python unpack_sekiro.py
"""

import subprocess
import os
import glob
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))           # .../Script
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)                          # .../Sekiro
TOOLS_DIR = os.path.join(PROJECT_DIR, "Tools")                     # .../Sekiro/Tools

YABBER = os.path.join(TOOLS_DIR, "Yabber 1.3.1", "Yabber.exe")
OUTPUT_DIR = os.path.join(PROJECT_DIR, "Extracted")

# Game directory: search common Steam locations
_GAME_PATHS = [
    r"F:\SteamLibrary\steamapps\common\Sekiro",
    r"D:\SteamLibrary\steamapps\common\Sekiro",
    r"C:\Program Files (x86)\Steam\steamapps\common\Sekiro",
]
GAME_DIR = None
for p in _GAME_PATHS:
    if os.path.isdir(p):
        GAME_DIR = p
        break
if GAME_DIR is None:
    raise FileNotFoundError("Sekiro game directory not found. Edit _GAME_PATHS in unpack_sekiro.py")

# Default Wolf parts (non-LOD versions)
PARTS = [
    "parts/hd_m_9520.partsbnd.dcx",    # Face and hair (tied up)
    "parts/bd_m_9000.partsbnd.dcx",    # Body
    "parts/am_m_9000.partsbnd.dcx",    # Arms
    "parts/lg_m_9000.partsbnd.dcx",    # Legs
]

# Character skeleton
CHR_FILES = [
    "chr/c0000.chrbnd.dcx",
]

def run_yabber(src_path, work_dir=None):
    """Run Yabber on a file. Yabber unpacks in-place next to the source."""
    if not os.path.exists(src_path):
        print(f"  SKIP (not found): {src_path}")
        return False

    # Check if already unpacked (folder exists)
    base = src_path
    for ext in [".dcx", ".partsbnd", ".chrbnd", ".anibnd", ".behbnd"]:
        base = base.replace(ext, "")
    unpack_dir = src_path.rsplit(".", 1)[0] if not src_path.endswith(".dcx") else src_path
    # Yabber creates folder like: file.partsbnd.dcx -> file-partsbnd-dcx/
    folder_name = os.path.basename(src_path).replace(".", "-")
    expected_dir = os.path.join(os.path.dirname(src_path), folder_name)

    if os.path.isdir(expected_dir):
        print(f"  SKIP (already unpacked): {expected_dir}")
        return True

    print(f"  Unpacking: {os.path.basename(src_path)}")
    try:
        result = subprocess.run(
            [YABBER, src_path],
            capture_output=True, text=True, timeout=120
        )
        if result.returncode != 0:
            print(f"  ERROR: {result.stderr.strip()}")
            return False
        return True
    except subprocess.TimeoutExpired:
        print(f"  TIMEOUT: {src_path}")
        return False
    except Exception as e:
        print(f"  ERROR: {e}")
        return False


def copy_and_unpack(rel_path, label=""):
    """Copy DCX to Extracted dir, then unpack with Yabber."""
    src = os.path.join(GAME_DIR, rel_path)
    dst = os.path.join(OUTPUT_DIR, os.path.basename(rel_path))

    if not os.path.exists(src):
        print(f"  NOT FOUND: {src}")
        return False

    # Copy if not exists
    if not os.path.exists(dst):
        import shutil
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        print(f"  Copied: {os.path.basename(dst)}")

    return run_yabber(dst)


def unpack_tpf_files(search_dir):
    """Find and unpack all .tpf files in unpacked directories."""
    tpf_files = glob.glob(os.path.join(search_dir, "**", "*.tpf"), recursive=True)
    count = 0
    for tpf in tpf_files:
        # Check if already unpacked
        tpf_folder = tpf.replace(".", "-")
        if os.path.isdir(tpf_folder):
            continue
        print(f"  Unpacking TPF: {os.path.basename(tpf)}")
        run_yabber(tpf)
        count += 1
    return count


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    # Phase 1: Character skeleton
    print("=" * 60)
    print("Phase 1: Character skeleton (c0000)")
    print("=" * 60)
    for f in CHR_FILES:
        copy_and_unpack(f)

    # Phase 2: Default parts
    print("\n" + "=" * 60)
    print("Phase 2: Default Wolf parts")
    print("=" * 60)
    for f in PARTS:
        copy_and_unpack(f)

    # Phase 3: All animation bundles
    print("\n" + "=" * 60)
    print("Phase 3: Animation bundles")
    print("=" * 60)
    chr_dir = os.path.join(GAME_DIR, "chr")
    anibnd_files = sorted(glob.glob(os.path.join(chr_dir, "c0000*.anibnd.dcx")))
    print(f"Found {len(anibnd_files)} animation bundles")

    for i, src in enumerate(anibnd_files):
        rel = os.path.relpath(src, GAME_DIR)
        print(f"[{i+1}/{len(anibnd_files)}] {os.path.basename(src)}")
        copy_and_unpack(rel)

    # Phase 4: Unpack TPF textures
    print("\n" + "=" * 60)
    print("Phase 4: Unpack TPF textures")
    print("=" * 60)
    n = unpack_tpf_files(OUTPUT_DIR)
    print(f"Unpacked {n} TPF files")

    # Summary
    print("\n" + "=" * 60)
    print("Summary")
    print("=" * 60)

    flver_count = len(glob.glob(os.path.join(OUTPUT_DIR, "**", "*.flver"), recursive=True))
    hkx_count = len(glob.glob(os.path.join(OUTPUT_DIR, "**", "*.hkx"), recursive=True))
    + len(glob.glob(os.path.join(OUTPUT_DIR, "**", "*.HKX"), recursive=True))
    dds_count = len(glob.glob(os.path.join(OUTPUT_DIR, "**", "*.dds"), recursive=True))
    tpf_count = len(glob.glob(os.path.join(OUTPUT_DIR, "**", "*.tpf"), recursive=True))

    print(f"FLVER files: {flver_count}")
    print(f"HKX files:   {hkx_count}")
    print(f"TPF files:   {tpf_count}")
    print(f"DDS files:   {dds_count}")
    print(f"\nAll assets extracted to: {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
