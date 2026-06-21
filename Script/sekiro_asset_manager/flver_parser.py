"""
FLVER Parser - wraps the C# FlverToJson.exe tool for per-file FLVER parsing.

Dependencies:
  - SoulsFormats (via FlverToJson.exe)
  - SoulsAssetPipeline (via FlverToJson.exe)
  - Havoc (HKX skeleton support)

Usage:
  from sekiro_asset_manager.flver_parser import FlverParser, fk_accumulate, flver_fk_accumulate
  data = FlverParser.parse("body.flver")
"""

import os
import json
import math
import subprocess
import tempfile
from pathlib import Path
from typing import Optional

_PROJECT_DIR = Path(__file__).resolve().parent.parent.parent
_FLVER_TO_JSON_EXE = _PROJECT_DIR / "Tools" / "FlverToJson" / "publish2" / "FlverToJson.exe"

# ======================================================================
# FLVER NodeFlags - match SoulsFormats.FLVER.Node.NodeFlags
# Disabled flag = Nub bone, replaced by HKX at runtime
# ======================================================================
class FlverNodeFlags:
    Disabled = 1 << 0

# ======================================================================
# Quaternion helpers (match DSAnimStudio / Havok conventions)
# ======================================================================
def _euler_to_quat_flver(rx, ry, rz):
    """Euler (FLVER order: RotX->RotZ->RotY) -> quaternion [x,y,z,w]"""
    cx, sx = math.cos(rx * 0.5), math.sin(rx * 0.5)
    cz, sz = math.cos(rz * 0.5), math.sin(rz * 0.5)
    cy, sy = math.cos(ry * 0.5), math.sin(ry * 0.5)
    return [
        cx * cz * sy + sx * sz * cy,
        cx * sz * cy + sx * cz * sy,
        cx * cz * cy - sx * sz * sy,
        cx * sz * sy - sx * cz * cy,
    ]

def _quat_rotate(q, v):
    qx, qy, qz, qw = q
    tx, ty, tz = 2*(qy*v[2]-qz*v[1]), 2*(qz*v[0]-qx*v[2]), 2*(qx*v[1]-qy*v[0])
    return [v[0]+qw*tx+(qy*tz-qz*ty), v[1]+qw*ty+(qz*tx-qx*tz), v[2]+qw*tz+(qx*ty-qy*tx)]

def _quat_mul(a, b):
    return [
        a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
        a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
        a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],
        a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2],
    ]

# ======================================================================
# FLVER FK Accumulate - match DSAnimStudio NewBone.GetBoneMatrix()
# Euler order: Scale * RotX * RotZ * RotY * Trans, recursive bottom-up
# ======================================================================
def flver_fk_accumulate(bones_data):
    """Compute FK world transforms for all bones using FLVER Euler local transforms."""
    n = len(bones_data)
    if n == 0:
        return
    for i, b in enumerate(bones_data):
        b["_idx"] = i
        if "ParentIndex" not in b:
            b["ParentIndex"] = -1
    world_cache = {}
    def compute_fk(idx):
        if idx in world_cache:
            return world_cache[idx]
        b = bones_data[idx]
        lp = b.get("LocalPos", [0,0,0])
        lr = b.get("LocalRot", [0,0,0])
        ls = b.get("LocalScale", [1,1,1])
        local_q = _euler_to_quat_flver(lr[0], lr[1], lr[2])
        pi = b.get("ParentIndex", -1)
        if 0 <= pi < n:
            pw, pr, ps = compute_fk(pi)
            scaled = [lp[0]*ps[0], lp[1]*ps[1], lp[2]*ps[2]]
            ro = _quat_rotate(pr, scaled)
            wp = [pw[0]+ro[0], pw[1]+ro[1], pw[2]+ro[2]]
            wr = _quat_mul(pr, local_q)
            ws = [ps[0]*ls[0], ps[1]*ls[1], ps[2]*ls[2]]
        else:
            wp, wr, ws = list(lp), local_q, list(ls)
        wc = (wp, wr, ws)
        world_cache[idx] = wc
        return wc
    for i in range(n):
        wp, wr, ws = compute_fk(i)
        bones_data[i]["WorldPos"] = [round(v,10) for v in wp]
        bones_data[i]["WorldRot"] = [round(v,10) for v in wr]
        bones_data[i]["WorldScale"] = [round(v,10) for v in ws]
    for b in bones_data:
        b.pop("_idx", None)

# ======================================================================
# HKX FK Accumulate - build skeleton from HKX animation data
# ======================================================================
def fk_accumulate(bone_names, bone_parents, bone_local_transforms):
    """Build skeleton bone list with FK world transforms from HKX animation data.

    Args:
        bone_names: list of bone name strings
        bone_parents: list of parent indices (-1 for root)
        bone_local_transforms: list of dicts with P[3], R[4](xyzw), S[3]

    Returns:
        list of bone dicts with Name, ParentIndex, ParentName,
        LocalPos, LocalRot, LocalScale, WorldPos, WorldRot, WorldScale,
        HkxIndex
    """
    n = len(bone_names)
    bones = []

    for i in range(n):
        lt = bone_local_transforms[i]
        local_pos = lt.get("P", [0, 0, 0])
        local_rot = lt.get("R", [0, 0, 0, 1])
        local_scale = lt.get("S", [1, 1, 1])
        bone = {
            "Name": bone_names[i],
            "ParentIndex": bone_parents[i],
            "ParentName": bone_names[bone_parents[i]] if bone_parents[i] >= 0 else "",
            "HkxIndex": i,
            "LocalPos": list(local_pos),
            "LocalRot": list(local_rot),
            "LocalScale": list(local_scale),
            "WorldPos": [0, 0, 0],
            "WorldRot": [0, 0, 0, 1],
            "WorldScale": [1, 1, 1],
        }
        bones.append(bone)

    world_cache = {}
    def compute_world(idx):
        if idx in world_cache:
            return world_cache[idx]
        b = bones[idx]
        lp = b["LocalPos"]
        lq = b["LocalRot"]
        ls = b["LocalScale"]
        pi = b["ParentIndex"]
        if pi >= 0 and pi < n:
            pw, pq, ps = compute_world(pi)
            scaled = [lp[0]*ps[0], lp[1]*ps[1], lp[2]*ps[2]]
            ro = _quat_rotate(pq, scaled)
            wp = [pw[0]+ro[0], pw[1]+ro[1], pw[2]+ro[2]]
            wq = _quat_mul(pq, lq)
            ws = [ps[0]*ls[0], ps[1]*ls[1], ps[2]*ls[2]]
        else:
            wp, wq, ws = list(lp), list(lq), list(ls)
        result = (wp, wq, ws)
        world_cache[idx] = result
        return result

    for i in range(n):
        wp, wq, ws = compute_world(i)
        bones[i]["WorldPos"] = [round(v, 10) for v in wp]
        bones[i]["WorldRot"] = [round(v, 10) for v in wq]
        bones[i]["WorldScale"] = [round(v, 10) for v in ws]

    return bones

# ======================================================================
# FlverParser - wraps C# FlverToJson.exe for per-file FLVER parsing
# ======================================================================
class FlverParser:
    """Parse individual FLVER files by calling the C# FlverToJson.exe tool.

    Since FlverToJson requires skeleton+body FLVERs together, we use
    a two-phase approach:
    1. Set the skeleton FLVER (for use as reference)
    2. Parse body FLVERs using the skeleton as reference
    """

    _skeleton_flver: Optional[str] = None
    _skeleton_hkx: Optional[str] = None

    @classmethod
    def set_skeleton(cls, skeleton_flver: str, skeleton_hkx: Optional[str] = None):
        """Set the skeleton FLVER path for subsequent body FLVER parsing."""
        cls._skeleton_flver = skeleton_flver
        cls._skeleton_hkx = skeleton_hkx

    @classmethod
    def _find_exe(cls) -> str:
        """Find FlverToJson.exe in various possible locations."""
        candidates = [
            _FLVER_TO_JSON_EXE,
            _PROJECT_DIR / "Tools" / "FlverToJson" / "publish2" / "FlverToJson.exe",
            _PROJECT_DIR / "Tools" / "FlverToJson" / "runner" / "FlverToJson.exe",
            _PROJECT_DIR / "Tools" / "FlverToJson" / "FlverToJson" / "bin" / "Release" / "net9.0-windows" / "FlverToJson.exe",
        ]
        for c in candidates:
            if c.exists():
                return str(c)
        raise FileNotFoundError(f"FlverToJson.exe not found in any expected location")

    @classmethod
    def _call_flver_to_fbx(cls, flver_paths: list[str], output_json: str,
                           skeleton_flver: Optional[str] = None,
                           skeleton_hkx: Optional[str] = None) -> bool:
        """Call FlverToJson.exe with given FLVER files."""
        exe = cls._find_exe()
        skel = skeleton_flver or cls._skeleton_flver
        if not skel:
            raise ValueError("No skeleton FLVER available for FlverToJson")

        cmd = [exe, skel] + flver_paths + ["-o", output_json]
        if skeleton_hkx or cls._skeleton_hkx:
            hkx = skeleton_hkx or cls._skeleton_hkx
            if os.path.exists(hkx):
                cmd += ["--skeleton-hkx", hkx]

        result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        if result.returncode != 0:
            print(f"  [FlverToJson stderr]: {result.stderr[-500:]}")
            return False
        return True

    @classmethod
    def parse(cls, flver_path: str) -> dict:
        """Parse a single FLVER file.

        For skeleton FLVER: sets it as reference and returns a minimal dict.
        For body FLVER: calls FlverToJson.exe with the skeleton and this body,
        then extracts this body's data from the combined output.
        """
        if not os.path.exists(flver_path):
            raise FileNotFoundError(f"FLVER not found: {flver_path}")

        part_name = os.path.splitext(os.path.basename(flver_path))[0]

        if cls._skeleton_flver is None:
            cls.set_skeleton(flver_path)
            return {
                "FileName": os.path.basename(flver_path),
                "Bones": [],
                "Materials": [],
                "Meshes": [],
                "Dummies": [],
                "BoundingBox": {"Min": [0,0,0], "Max": [0,0,0]},
            }

        with tempfile.NamedTemporaryFile(suffix=".json", delete=False, mode="w") as tmp:
            tmp_path = tmp.name

        try:
            if not cls._call_flver_to_fbx([flver_path], tmp_path):
                raise RuntimeError(f"FlverToJson failed for {flver_path}")
            with open(tmp_path, "r", encoding="utf-8") as f:
                combined = json.load(f)
        finally:
            try:
                os.unlink(tmp_path)
            except OSError:
                pass

        part_meshes = []
        part_materials = []
        part_material_names = set()

        for mesh in combined.get("Meshes", []):
            mesh_part = mesh.get("Part", "")
            if mesh_part == part_name:
                part_meshes.append(mesh)
                mi = mesh.get("MaterialIndex", 0)
                global_mats = combined.get("Materials", [])
                if mi < len(global_mats):
                    mat = global_mats[mi]
                    mat_name = mat.get("Name", "")
                    if mat_name not in part_material_names:
                        part_material_names.add(mat_name)
                        part_materials.append(mat)

        if not part_meshes:
            part_meshes = list(combined.get("Meshes", []))
            part_materials = list(combined.get("Materials", []))

        return {
            "FileName": os.path.basename(flver_path),
            "Bones": combined.get("Bones", []),
            "Materials": part_materials,
            "Meshes": part_meshes,
            "Dummies": combined.get("Dummies", []),
            "BoundingBox": combined.get("BoundingBox", {"Min": [0,0,0], "Max": [0,0,0]}),
        }

    @classmethod
    def parse_all(cls, flver_paths: list[str], skeleton_flver: str,
                  skeleton_hkx: Optional[str] = None,
                  output_json: Optional[str] = None) -> dict:
        """Parse all FLVER files at once (efficient batch call).

        Returns the full combined JSON from FlverToJson.
        """
        cls.set_skeleton(skeleton_flver, skeleton_hkx)

        if output_json is None:
            with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tmp:
                output_json = tmp.name
            cleanup = True
        else:
            cleanup = False

        try:
            if not cls._call_flver_to_fbx(flver_paths, output_json, skeleton_flver, skeleton_hkx):
                raise RuntimeError("FlverToJson batch call failed")
            with open(output_json, "r", encoding="utf-8") as f:
                result = json.load(f)
            return result
        finally:
            if cleanup:
                try:
                    os.unlink(output_json)
                except OSError:
                    pass
