"""
Sekiro 资产管线配置与工具解析。

统一负责：
1. 读取项目路径、游戏路径、引擎路径
2. 解析 `Script/sekiro_asset_manager/ext_tools` 下的工具
3. 对有源码的外部工具在缺少可执行文件时尝试自动编译

约定：
- `sekiro_asset_manager` 只引用 `ext_tools` 下的工具
- 优先使用 `ext_tools/<Tool>/bin/Release/...` 输出
- 对没有源码的工具（例如已发布的 FlverToJson）仅查找现成二进制
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from dataclasses import dataclass


_PROJECT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
)


@dataclass(frozen=True)
class ToolSpec:
    """描述一个 ext_tools 工具的可执行文件位置与源码信息。"""

    candidates: tuple[str, ...]
    project: str | None = None
    build_configuration: str = "Release"
    source_dir: str | None = None
    source_repo_dir: str | None = None
    tool_fallbacks: tuple[str, ...] = ()


_DEFAULT_GAME_PATHS = (
    r"F:\SteamLibrary\steamapps\common\Sekiro",
    r"D:\SteamLibrary\steamapps\common\Sekiro",
    r"C:\Program Files (x86)\Steam\steamapps\common\Sekiro",
)

_DEFAULT_UE_ENGINE_DIRS = (
    r"F:\UnrealEngine-5.2",
    r"D:\UnrealEngine-5.2",
)

_SETTINGS_CANDIDATES = (
    os.path.join(_PROJECT_DIR, ".codex", "settings.local.json"),
    os.path.join(_PROJECT_DIR, ".claude", "settings.local.json"),
)

_TOOL_ALIASES = {
    "flver_to_fbx": "flver_to_json",
}

_TOOL_SPECS: dict[str, ToolSpec] = {
    "yabber": ToolSpec(
        candidates=(
            os.path.join("Yabber", "Yabber.exe"),
        ),
        tool_fallbacks=(
            os.path.join("Yabber 1.3.1", "Yabber.exe"),
        ),
    ),
    "texconv": ToolSpec(
        candidates=(
            os.path.join("texconv", "texconv.exe"),
        ),
        tool_fallbacks=(
            "texconv.exe",
        ),
    ),
    "flver_to_json": ToolSpec(
        candidates=(
            os.path.join("FlverToJson", "bin", "Release", "net9.0-windows", "FlverToFbx.exe"),
            os.path.join("FlverToJson", "bin", "Release", "net9.0-windows", "FlverToJson.exe"),
            os.path.join("FlverToJson", "FlverToFbx.exe"),
            os.path.join("FlverToJson", "FlverToJson.exe"),
        ),
        project=os.path.join("FlverToJson", "FlverToFbx.csproj"),
        source_dir="FlverToJson",
        source_repo_dir=os.path.join("FlverToFbx", "FlverToFbx"),
        tool_fallbacks=(
            os.path.join("FlverToFbx", "FlverToFbx", "bin", "Release", "net9.0-windows", "FlverToFbx.exe"),
            os.path.join("FlverToFbx", "FlverToFbx", "bin", "Release", "net9.0", "FlverToFbx.exe"),
        ),
    ),
    "sekiro_anim_extractor": ToolSpec(
        candidates=(
            os.path.join("SekiroAnimExtractor", "bin", "Release", "net9.0-windows", "SekiroAnimExtractor.exe"),
            os.path.join("SekiroAnimExtractor", "SekiroAnimExtractor.exe"),
        ),
        project=os.path.join("SekiroAnimExtractor", "SekiroAnimExtractor.csproj"),
        source_dir="SekiroAnimExtractor",
    ),
    "sekiro_tae_extractor": ToolSpec(
        candidates=(
            os.path.join("SekiroTAEExtractor", "bin", "Release", "net9.0-windows", "SekiroTAEExtractor.exe"),
            os.path.join("SekiroTAEExtractor", "bin", "Debug", "net9.0-windows", "SekiroTAEExtractor.exe"),
            os.path.join("SekiroTAEExtractor", "SekiroTAEExtractor.exe"),
        ),
        project=os.path.join("SekiroTAEExtractor", "SekiroTAEExtractor.csproj"),
        source_dir="SekiroTAEExtractor",
    ),
    "param_reader": ToolSpec(
        candidates=(
            os.path.join("ParamReader", "bin", "Release", "net9.0", "ParamReader.exe"),
            os.path.join("ParamReader", "ParamReader.exe"),
        ),
        project=os.path.join("ParamReader", "ParamReader.csproj"),
        source_dir="ParamReader",
    ),
    "tae_to_json": ToolSpec(
        candidates=(
            os.path.join("TaeToJson", "TaeToJson", "bin", "Release", "net9.0", "TaeToJson.exe"),
            os.path.join("TaeToJson", "TaeToJson.exe"),
        ),
        project=os.path.join("TaeToJson", "TaeToJson", "TaeToJson.csproj"),
        source_dir="TaeToJson",
    ),
}


class PipelineConfig:
    """项目路径与工具路径统一入口。"""

    def __init__(self, settings_path: str | None = None):
        self._project_dir = _PROJECT_DIR
        self._settings = {}
        self._tool_cache: dict[tuple[str, bool], str] = {}

        if settings_path is None:
            for candidate in _SETTINGS_CANDIDATES:
                if os.path.exists(candidate):
                    settings_path = candidate
                    break

        if settings_path and os.path.exists(settings_path):
            with open(settings_path, "r", encoding="utf-8") as file:
                self._settings = json.load(file)

        self._env = self._settings.get("env", {})

    @property
    def project_dir(self) -> str:
        return self._project_dir

    @property
    def tools_dir(self) -> str:
        return os.path.join(self._project_dir, "Script", "sekiro_asset_manager", "ext_tools")

    @property
    def extracted_dir(self) -> str:
        return os.path.join(self._project_dir, "Extracted")

    @property
    def output_dir(self) -> str:
        return os.path.join(self._project_dir, "Output")

    @property
    def content_dir(self) -> str:
        return os.path.join(self._project_dir, "Content")

    @property
    def scripts_dir(self) -> str:
        return os.path.join(self._project_dir, "Script")

    @property
    def ue_content_root(self) -> str:
        if env_value := self._env.get("SK_UE_CONTENT_ROOT"):
            return env_value
        return "/Game/Character"

    @property
    def game_dir(self) -> str:
        if env_value := self._env.get("SEKIRO_GAME_DIR"):
            if os.path.exists(env_value):
                return env_value
        for candidate in _DEFAULT_GAME_PATHS:
            if os.path.exists(candidate):
                return candidate
        return ""

    @property
    def engine_dir(self) -> str:
        if env_value := self._env.get("UE_ENGINE_DIR"):
            if os.path.exists(env_value):
                return env_value
        for candidate in _DEFAULT_UE_ENGINE_DIRS:
            if os.path.exists(candidate):
                return candidate
        return ""

    def _normalize_tool_name(self, tool_name: str) -> str:
        return _TOOL_ALIASES.get(tool_name, tool_name)

    def _tool_spec(self, tool_name: str) -> ToolSpec | None:
        return _TOOL_SPECS.get(self._normalize_tool_name(tool_name))

    def _tool_candidate_paths(self, tool_name: str) -> list[str]:
        spec = self._tool_spec(tool_name)
        if spec is None:
            return []
        return [os.path.join(self.tools_dir, rel_path) for rel_path in spec.candidates]

    def _project_path(self, relative_path: str | None) -> str:
        if not relative_path:
            return ""
        return os.path.join(self.tools_dir, relative_path)

    def _tools_repo_dir(self) -> str:
        return os.path.join(self.project_dir, "Tools")

    def _sync_tool_source_from_tools(self, tool_name: str) -> bool:
        spec = self._tool_spec(tool_name)
        if spec is None or not spec.source_dir:
            return False

        dst_dir = os.path.join(self.tools_dir, spec.source_dir)
        if spec.project and os.path.exists(self._project_path(spec.project)):
            return True

        repo_dir = spec.source_repo_dir or spec.source_dir
        src_dir = os.path.join(self._tools_repo_dir(), repo_dir)
        if not os.path.isdir(src_dir):
            return False

        def ignore_entries(_src: str, names: list[str]) -> set[str]:
            ignored = {"bin", "obj", ".vs", ".git"}
            return {name for name in names if name in ignored}

        os.makedirs(self.tools_dir, exist_ok=True)
        shutil.copytree(src_dir, dst_dir, dirs_exist_ok=True, ignore=ignore_entries)
        return bool(spec.project and os.path.exists(self._project_path(spec.project)))

    def _sync_tool_binary_from_tools(self, tool_name: str) -> str:
        spec = self._tool_spec(tool_name)
        if spec is None:
            return ""

        os.makedirs(self.tools_dir, exist_ok=True)
        for fallback in spec.tool_fallbacks:
            src_path = os.path.join(self._tools_repo_dir(), fallback)
            if not os.path.exists(src_path):
                continue

            if os.path.isdir(src_path):
                dst_dir = os.path.join(self.tools_dir, os.path.basename(src_path))
                shutil.copytree(src_path, dst_dir, dirs_exist_ok=True)
            else:
                dst_candidate = self._tool_candidate_paths(tool_name)
                if dst_candidate:
                    dst_path = dst_candidate[0]
                    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
                    shutil.copy2(src_path, dst_path)
            found = self._find_existing_tool(tool_name)
            if found:
                return found
        return ""

    def _find_existing_tool(self, tool_name: str) -> str:
        for candidate in self._tool_candidate_paths(tool_name):
            if os.path.exists(candidate):
                return candidate
        return ""

    def _ensure_tool_built(self, tool_name: str) -> str:
        spec = self._tool_spec(tool_name)
        if spec is None or not spec.project:
            return ""

        self._sync_tool_source_from_tools(tool_name)
        project_path = self._project_path(spec.project)
        if not os.path.exists(project_path):
            return ""

        dotnet = shutil.which("dotnet")
        if not dotnet:
            return ""

        try:
            result = subprocess.run(
                [
                    dotnet,
                    "build",
                    project_path,
                    "-c",
                    spec.build_configuration,
                ],
                cwd=os.path.dirname(project_path),
                capture_output=True,
                text=True,
                timeout=300,
            )
        except (OSError, subprocess.TimeoutExpired):
            return ""

        if result.returncode != 0:
            return ""

        return self._find_existing_tool(tool_name)

    def tool_path(self, tool_name: str, ensure_built: bool = True) -> str:
        """返回工具可执行文件路径。

        对有源码的工具，若 `ensure_built=True` 且未找到可执行文件，会尝试自动编译。
        """

        normalized = self._normalize_tool_name(tool_name)
        cache_key = (normalized, ensure_built)
        cached = self._tool_cache.get(cache_key)
        if cached and os.path.exists(cached):
            return cached

        if normalized == "blender":
            path = self.get_blender()
            self._tool_cache[cache_key] = path
            return path

        if normalized == "ue_editor":
            path = self.get_unreal_editor_cmd()
            self._tool_cache[cache_key] = path
            return path

        path = self._find_existing_tool(normalized)
        if not path:
            path = self._sync_tool_binary_from_tools(normalized)
        if not path and ensure_built:
            path = self._ensure_tool_built(normalized)

        self._tool_cache[cache_key] = path
        return path

    def tae_extractor_path(self) -> str:
        return self.tool_path("sekiro_tae_extractor")

    def get_python(self) -> str:
        return os.path.join(
            self.engine_dir,
            "Engine",
            "Binaries",
            "ThirdParty",
            "Python3",
            "Win64",
            "python.exe",
        )

    def get_unreal_editor_cmd(self) -> str:
        return os.path.join(
            self.engine_dir,
            "Engine",
            "Binaries",
            "Win64",
            "UnrealEditor-Cmd.exe",
        )

    def get_blender(self) -> str:
        if env_value := self._env.get("BLENDER_PATH"):
            if os.path.exists(env_value):
                return env_value

        candidates = (
            r"C:\Program Files\Blender Foundation\Blender 4.2\blender.exe",
            r"C:\Program Files\Blender Foundation\Blender 3.6\blender.exe",
            r"C:\Program Files\Blender Foundation\Blender 4.1\blender.exe",
            r"C:\Program Files\Blender Foundation\Blender 4.0\blender.exe",
        )
        for candidate in candidates:
            if os.path.exists(candidate):
                return candidate

        return "blender"

    def get_bridge_py(self) -> str:
        candidates = (
            os.path.join(self._project_dir, "Script", "aibridge", "bridge.py"),
            os.path.join(self._project_dir, ".codex", "skills", "aibridge", "bridge.py"),
            os.path.join(self._project_dir, ".claude", "skills", "aibridge", "bridge.py"),
        )
        for candidate in candidates:
            if os.path.exists(candidate):
                return candidate
        return candidates[0]

    def output_model_dir(self, prefix: str) -> str:
        return os.path.join(self.output_dir, prefix, "Model")

    def output_anim_dir(self, prefix: str) -> str:
        return os.path.join(self.output_dir, prefix, "Animation")

    def output_material_dir(self, prefix: str) -> str:
        return os.path.join(self.output_dir, prefix, "Material")

    def output_model_json(self, prefix: str) -> str:
        return os.path.join(self.output_model_dir(prefix), f"{prefix}_model.json")

    def output_anim_json(self, prefix: str) -> str:
        return os.path.join(self.output_anim_dir(prefix), f"{prefix}_common_anims.json")

    def output_material_json(self, prefix: str) -> str:
        return os.path.join(self.output_material_dir(prefix), f"{prefix}_Materials.json")


config = PipelineConfig()
