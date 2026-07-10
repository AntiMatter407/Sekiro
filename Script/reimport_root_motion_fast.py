from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Any

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT_DIR = os.path.join(PROJECT_DIR, "Script")
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from sekiro_asset_manager.animation_importer import AnimationImporter
from sekiro_asset_manager.pipeline_config import config as pipeline_config

try:
    sys.stdout.reconfigure(line_buffering=True)
except AttributeError:
    pass


@dataclass
class PackageJob:
    name: str
    directory: str
    anim_dir: str
    output_json: str


def run_command(cmd: list[str], cwd: str | None = None, timeout: int = 600) -> bool:
    print(" ".join(cmd))
    run = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)
    if run.stdout:
        print(run.stdout[-3000:])
    if run.returncode != 0:
        if run.stderr:
            print(run.stderr[-3000:])
        return False
    return True


def build_extractor() -> bool:
    dotnet = shutil.which("dotnet")
    if not dotnet:
        print("[Error] dotnet not found in PATH")
        return False

    project = os.path.join(
        pipeline_config.tools_dir,
        "SekiroAnimExtractor",
        "SekiroAnimExtractor.csproj",
    )
    if not os.path.exists(project):
        print(f"[Error] SekiroAnimExtractor project not found: {project}")
        return False

    return run_command(
        [dotnet, "build", project, "-c", "Release", "-v", "q"],
        cwd=os.path.dirname(project),
        timeout=300,
    )


def find_skeleton_hkx(base_asset: str) -> str:
    skeleton_dir = os.path.join(pipeline_config.extracted_dir, f"{base_asset}-anibnd-dcx")
    for root, _dirs, files in os.walk(skeleton_dir):
        for file_name in files:
            if file_name.lower() == "skeleton.hkx":
                return os.path.join(root, file_name)
    return ""


def has_anim_hkx(directory: str) -> bool:
    for root, _dirs, files in os.walk(directory):
        if os.path.basename(root).lower() == "hkx" and "skeleton.hkx" in [file_name.lower() for file_name in files]:
            continue
        for file_name in files:
            lower = file_name.lower()
            if lower.endswith(".hkx") and lower != "skeleton.hkx":
                return True
    return False


def find_anim_dir(package_dir: str) -> str:
    best_dir = ""
    best_count = 0
    for root, _dirs, files in os.walk(package_dir):
        count = sum(1 for file_name in files if file_name.lower().endswith(".hkx") and file_name.lower() != "skeleton.hkx")
        if count > best_count:
            best_dir = root
            best_count = count

    if best_dir:
        parent = os.path.dirname(best_dir)
        if os.path.basename(best_dir).lower().endswith("_compendium") and os.path.basename(parent).lower() == "hkx":
            return parent
        return best_dir
    return ""


def package_output_json(base_asset: str, package_name: str) -> str:
    output_dir = pipeline_config.output_anim_dir(base_asset)
    os.makedirs(output_dir, exist_ok=True)
    return os.path.join(output_dir, f"Sekiro_anims_{package_name}.json")


def discover_jobs(base_asset: str, package_filter: set[str]) -> list[PackageJob]:
    jobs: list[PackageJob] = []
    extracted = pipeline_config.extracted_dir
    prefix = f"{base_asset}"

    for entry in sorted(os.listdir(extracted)):
        if not entry.startswith(prefix) or not entry.endswith("-anibnd-dcx"):
            continue

        package_name = entry.removesuffix("-anibnd-dcx")
        if package_name == base_asset:
            continue
        if package_filter and package_name not in package_filter:
            continue

        package_dir = os.path.join(extracted, entry)
        if not os.path.isdir(package_dir) or not has_anim_hkx(package_dir):
            continue

        anim_dir = find_anim_dir(package_dir)
        if not anim_dir:
            continue

        jobs.append(PackageJob(
            name=package_name,
            directory=package_dir,
            anim_dir=anim_dir,
            output_json=package_output_json(base_asset, package_name),
        ))

    return jobs


def strip_sekiro_prefix(output_json: str) -> None:
    with open(output_json, "r", encoding="utf-8") as file:
        data: dict[str, Any] = json.load(file)

    changed = False
    for anim in data.get("Animations", []):
        name = anim.get("Name", "")
        if name.startswith("Sekiro_"):
            anim["Name"] = name[7:]
            changed = True

    if changed:
        with open(output_json, "w", encoding="utf-8") as file:
            json.dump(data, file, separators=(",", ":"), ensure_ascii=False)


def run_extractor(extractor: str, skeleton_hkx: str, job: PackageJob, sample_rate: int) -> bool:
    cmd = [
        extractor,
        skeleton_hkx,
        job.anim_dir,
        job.output_json,
        "--sample-rate",
        str(sample_rate),
    ]
    if not run_command(cmd, timeout=900):
        return False
    if not os.path.exists(job.output_json):
        print(f"[Error] Animation JSON not generated: {job.output_json}")
        return False

    strip_sekiro_prefix(job.output_json)
    return True


def run_md_fix(importer: AnimationImporter, job: PackageJob) -> str:
    if not job.name.endswith("_md") and "_md_" not in job.name:
        return job.output_json

    combined_json = os.path.splitext(job.output_json)[0] + "_fullpose_combined.json"
    if importer.run_md_fix(job.output_json, combined_json):
        return combined_json

    print(f"[Warn] MD fix failed, fallback to raw JSON: {job.output_json}")
    return job.output_json


def import_to_ue(importer: AnimationImporter, import_json: str, args: argparse.Namespace) -> bool:
    if args.anim_names:
        ue_cmd = pipeline_config.get_unreal_editor_cmd()
        uproject = os.path.join(pipeline_config.project_dir, "Sekiro.uproject")
        cmd = [
            ue_cmd,
            uproject,
            "-run=SAImport",
            f"-Anim={import_json}",
            f"-Output={args.ue_path}",
            f"-Skeleton={args.skeleton}",
            f"-AssetName={args.asset_prefix}",
            f"-AnimName={','.join(args.anim_names)}",
            "-unattended",
            "-NoSplash",
            "-NoP4",
        ]
        return run_command(cmd, timeout=1800)

    return importer.run_ue_import(
        anim_json=import_json,
        target_path=args.ue_path,
        asset_name=args.asset_prefix,
        skeleton_name=args.skeleton,
    )


def open_at_animations_array(json_path: str) -> tuple[Any, str]:
    file = open(json_path, "r", encoding="utf-8")
    marker = '"Animations"'
    prefix: list[str] = []
    matched = 0

    try:
        while True:
            char = file.read(1)
            if not char:
                raise RuntimeError(f"Animations field not found: {json_path}")

            prefix.append(char)
            if char == marker[matched]:
                matched += 1
                if matched == len(marker):
                    break
            else:
                matched = 1 if char == marker[0] else 0

        while True:
            char = file.read(1)
            if not char:
                raise RuntimeError(f"Animations array not found: {json_path}")

            prefix.append(char)
            if char == "[":
                break

        return file, "".join(prefix)
    except Exception:
        file.close()
        raise


def copy_animation_array_items(src_file: Any, dst_file: Any, has_global_item: bool) -> tuple[bool, int]:
    in_string = False
    escape = False
    depth = 0
    started = False
    count = 0

    while True:
        char = src_file.read(1)
        if not char:
            raise RuntimeError("Unexpected EOF while copying animation array")

        if in_string:
            if started:
                dst_file.write(char)

            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == '"':
                in_string = False
            continue

        if depth == 0 and char == "]":
            break

        if not started:
            if char.isspace() or char == ",":
                continue

            if has_global_item:
                dst_file.write(",")
            started = True

        if char == '"':
            in_string = True
            dst_file.write(char)
            continue

        if char in "[{":
            if depth == 0 and char == "{":
                count += 1
            depth += 1
            dst_file.write(char)
            continue

        if char in "]}":
            dst_file.write(char)
            if depth > 0:
                depth -= 1
            continue

        dst_file.write(char)

    return has_global_item or started, count


def merge_anim_jsons(base_asset: str, import_jsons: list[str]) -> str:
    if not import_jsons:
        raise RuntimeError("No animation JSON to merge")

    output_dir = pipeline_config.output_anim_dir(base_asset)
    os.makedirs(output_dir, exist_ok=True)
    merged_json = os.path.join(output_dir, f"Sekiro_anims_{base_asset}_rootmotion_all.json")

    total_count = 0
    has_global_item = False
    with open(merged_json, "w", encoding="utf-8") as output_file:
        first_file, prefix = open_at_animations_array(import_jsons[0])
        try:
            output_file.write(prefix)
            has_global_item, count = copy_animation_array_items(first_file, output_file, has_global_item)
            total_count += count
        finally:
            first_file.close()

        for import_json in import_jsons[1:]:
            src_file, _prefix = open_at_animations_array(import_json)
            try:
                has_global_item, count = copy_animation_array_items(src_file, output_file, has_global_item)
                total_count += count
            finally:
                src_file.close()

        output_file.write("]}\n")

    print(f"[Merge] {total_count} animations -> {merged_json}")
    return merged_json


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fast re-extract and reimport Sekiro animation packages with RootMotion.",
    )
    parser.add_argument("--base-asset", default="c0000")
    parser.add_argument("--packages", nargs="*", default=[], help="Package names, for example c0000_a000_lo")
    parser.add_argument("--sample-rate", type=int, default=30)
    parser.add_argument("--ue-path", default="/Game/Characters/Sekiro")
    parser.add_argument("--asset-prefix", default="Sekiro")
    parser.add_argument("--skeleton", default="Sekiro_Skeleton")
    parser.add_argument("--anim-names", nargs="*", default=[])
    parser.add_argument("--skip-import", action="store_true")
    parser.add_argument("--skip-extract", action="store_true")
    parser.add_argument("--skip-md-fix", action="store_true")
    parser.add_argument("--no-build-tools", action="store_true")
    parser.add_argument("--per-package-import", action="store_true")
    parser.add_argument("--max-packages", type=int, default=0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    start_time = time.time()

    if not args.no_build_tools and not build_extractor():
        return 1

    extractor = pipeline_config.tool_path("sekiro_anim_extractor", ensure_built=not args.no_build_tools)
    if not extractor or not os.path.exists(extractor):
        print(f"[Error] SekiroAnimExtractor not found: {extractor}")
        return 1

    skeleton_hkx = find_skeleton_hkx(args.base_asset)
    if not skeleton_hkx:
        print(f"[Error] skeleton.hkx not found for {args.base_asset}")
        return 1

    jobs = discover_jobs(args.base_asset, set(args.packages))
    if args.max_packages > 0:
        jobs = jobs[:args.max_packages]
    if not jobs:
        print("[Error] No animation packages found")
        return 1

    importer = AnimationImporter(pipeline_config)
    failed: list[str] = []
    import_jsons: list[str] = []

    print(f"[RootMotion] packages={len(jobs)} skeleton={skeleton_hkx}")
    for index, job in enumerate(jobs, start=1):
        print(f"[{index}/{len(jobs)}] {job.name}")

        if not args.skip_extract and not run_extractor(extractor, skeleton_hkx, job, args.sample_rate):
            failed.append(job.name)
            continue

        import_json = job.output_json
        if not args.skip_md_fix:
            import_json = run_md_fix(importer, job)

        if args.skip_import:
            import_jsons.append(import_json)
            continue

        if args.per_package_import and not import_to_ue(importer, import_json, args):
            failed.append(job.name)
            continue
        if not args.per_package_import:
            import_jsons.append(import_json)

    if not args.skip_import and not args.per_package_import and import_jsons:
        merged_json = merge_anim_jsons(args.base_asset, import_jsons)
        if not import_to_ue(importer, merged_json, args):
            failed.append("combined_ue_import")

    elapsed = time.time() - start_time
    if failed:
        print(f"[Done] failed={len(failed)} elapsed={elapsed:.1f}s")
        for name in failed:
            print(f"  - {name}")
        return 1

    print(f"[Done] success={len(jobs)} elapsed={elapsed:.1f}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
