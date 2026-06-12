"""
Convert DDS textures to PNG using Microsoft DirectXTex texconv.exe.
Usage:
  python convert_dds_texconv.py <source_dir_or_files...> [-o output_dir] [--texconv path/to/texconv.exe]

The script auto-locates texconv.exe from:
  1. --texconv argument
  2. <project>/Tools/texconv.exe
  3. PATH

Supports all DDS formats: BC1-BC7, DX10, ATI1/2, uncompressed.
Output is always RGBA8 PNG.
"""
import os
import sys
import subprocess
import argparse


def find_texconv(explicit=None):
    if explicit and os.path.isfile(explicit):
        return explicit
    # Project Tools directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    candidate = os.path.join(project_root, "Tools", "texconv.exe")
    if os.path.isfile(candidate):
        return candidate
    # PATH
    import shutil
    found = shutil.which("texconv")
    if found:
        return found
    return None


def main():
    parser = argparse.ArgumentParser(description="Convert DDS to PNG via texconv")
    parser.add_argument("sources", nargs="+", help="DDS files or directories")
    parser.add_argument("-o", "--output", default=None, help="Output directory (default: same as source)")
    parser.add_argument("--texconv", default=None, help="Path to texconv.exe")
    parser.add_argument("-y", "--overwrite", action="store_true", help="Overwrite existing PNG files")
    args = parser.parse_args()

    texconv = find_texconv(args.texconv)
    if not texconv:
        print("ERROR: texconv.exe not found. Place it in <project>/Tools/ or pass --texconv.")
        sys.exit(1)

    # Collect all DDS files
    dds_files = []
    for src in args.sources:
        if os.path.isfile(src):
            if src.lower().endswith(".dds"):
                dds_files.append(src)
        elif os.path.isdir(src):
            for root, dirs, files in os.walk(src):
                for f in files:
                    if f.lower().endswith(".dds"):
                        dds_files.append(os.path.join(root, f))

    if not dds_files:
        print("No DDS files found.")
        return

    cmd = [texconv, "-ft", "png"]
    if args.overwrite:
        cmd.append("-y")
    if args.output:
        os.makedirs(args.output, exist_ok=True)
        cmd.extend(["-o", args.output])
    cmd.extend(["--"] + dds_files)

    print(f"Converting {len(dds_files)} DDS files...")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.stdout:
        print(result.stdout)
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    if result.returncode != 0:
        print(f"texconv exited with code {result.returncode}")
        sys.exit(result.returncode)
    print("Done.")


if __name__ == "__main__":
    main()
