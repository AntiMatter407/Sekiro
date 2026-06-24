"""
Sekiro 资产管线配置管理模块。

负责从 settings.local.json、环境变量和默认值三个来源合并读取路径配置。
提供统一的工具路径、输出路径和辅助路径查询接口。
"""

import os
import json


# 项目根目录（Script/sekiro_asset_manager/ 向上3层 -> 项目根）
_PROJECT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
)

# ---- 默认路径配置 ----

DEFAULT_PATHS = {
    "tools_dir": os.path.join(_PROJECT_DIR, "Script", "sekiro_asset_manager", "ext_tools"),
    "extracted_dir": os.path.join(_PROJECT_DIR, "Extracted"),
    "output_dir": os.path.join(_PROJECT_DIR, "Output"),
    "content_dir": os.path.join(_PROJECT_DIR, "Content"),
    "scripts_dir": os.path.join(_PROJECT_DIR, "Script"),
}

# ---- 工具路径（相对于 Tools/ 的子目录结构） ----

DEFAULT_TOOLS = {
    "yabber": ("Yabber", "Yabber.exe"),
    "texconv": ("texconv", "texconv.exe"),
    "flver_to_fbx": ("FlverToJson", "FlverToJson.exe"),
    "sekiro_anim_extractor": ("SekiroAnimExtractor", "SekiroAnimExtractor.exe"),
    "sekiro_tae_extractor": ("SekiroTAEExtractor", "SekiroTAEExtractor.exe"),
}

# ---- 默认游戏路径（Steam 常见安装位置） ----

_DEFAULT_GAME_PATHS = [
    r"F:\SteamLibrary\steamapps\common\Sekiro",
    r"D:\SteamLibrary\steamapps\common\Sekiro",
    r"C:\Program Files (x86)\Steam\steamapps\common\Sekiro",
]

# ---- 默认 UE 引擎路径 ----

_DEFAULT_UE_ENGINE_DIRS = [
    r"F:\UnrealEngine-5.2",
    r"D:\UnrealEngine-5.2",
]


class PipelineConfig:
    """管线配置，从多个来源合并路径设置。

    优先级：settings.local.json > 环境变量 > 默认路径列表（按序检测是否存在）。
    """

    def __init__(self, settings_path: str = None):
        """初始化配置管理器。

        Args:
            settings_path: settings.local.json 的完整路径。
                          为 None 时自动查找项目默认位置。
        """
        self._project_dir = _PROJECT_DIR
        # 来自 settings.local.json 的原始配置
        self._settings = {}

        if settings_path is None:
            # 默认位置：.claude/settings.local.json
            default_path = os.path.join(
                self._project_dir, ".claude", "settings.local.json"
            )
            if os.path.exists(default_path):
                settings_path = default_path

        if settings_path and os.path.exists(settings_path):
            with open(settings_path, "r", encoding="utf-8") as f:
                self._settings = json.load(f)

        # 提取 settings 中的 env 段，方便多次读取
        self._env = self._settings.get("env", {})

    # ---- 核心目录属性 ----

    @property
    def project_dir(self) -> str:
        """项目根目录（Sekiro.uproject 所在目录）。"""
        return self._project_dir

    @property
    def tools_dir(self) -> str:
        """工具根目录（Tools/）。"""
        return os.path.join(self._project_dir, "Tools")

    @property
    def extracted_dir(self) -> str:
        """解包输出根目录（Extracted/）。"""
        return os.path.join(self._project_dir, "Extracted")

    @property
    def output_dir(self) -> str:
        """结构化输出根目录（Output/）。按前缀→类型组织。"""
        return os.path.join(self._project_dir, "Output")

    @property
    def content_dir(self) -> str:
        """UE Content 目录（Content/）。"""
        return os.path.join(self._project_dir, "Content")

    @property
    def ue_content_root(self) -> str:
        """UE Content Browser ?????????
        ??? settings.local.json ? SK_UE_CONTENT_ROOT ???????
        ??: /Game/Sekiro/Character
        """
        if env_val := self._env.get("SK_UE_CONTENT_ROOT"):
            return env_val
        return "/Game/Character"

    @property
    def scripts_dir(self) -> str:
        """脚本根目录（Script/）。"""
        return os.path.join(self._project_dir, "Script")

    # ---- 运行时目录探测 ----

    @property
    def game_dir(self) -> str:
        """只狼游戏安装目录。

        探测顺序：
        1. settings.local.json 中 SEKIRO_GAME_DIR 环境变量覆盖
        2. 常见 Steam 安装路径列表
        """
        if env_val := self._env.get("SEKIRO_GAME_DIR"):
            if os.path.exists(env_val):
                return env_val
        for path in _DEFAULT_GAME_PATHS:
            if os.path.exists(path):
                return path
        return ""

    @property
    def engine_dir(self) -> str:
        """UE5 引擎目录。

        探测顺序：
        1. settings.local.json 中 UE_ENGINE_DIR 环境变量覆盖
        2. 常见引擎安装路径列表
        """
        if env_val := self._env.get("UE_ENGINE_DIR"):
            if os.path.exists(env_val):
                return env_val
        for path in _DEFAULT_UE_ENGINE_DIRS:
            if os.path.exists(path):
                return path
        return ""

    # ---- 工具路径查询 ----

    def tool_path(self, tool_name: str) -> str:
        """返回指定工具的可执行文件完整路径。

        Args:
            tool_name: 工具名称，须为 DEFAULT_TOOLS 中的键名。

        Returns:
            完整路径字符串。如果工具名称不存在则返回空字符串。
        """
        if tool_name not in DEFAULT_TOOLS:
            return ""
        parts = DEFAULT_TOOLS[tool_name]
        return os.path.join(self.tools_dir, *parts)

    def tae_extractor_path(self) -> str:
        """返回 SekiroTAEExtractor 可执行文件路径。

        该工具位于 Script/sekiro_asset_manager/ext_tools/ 下。
        """
        return os.path.join(
            self._project_dir, "Script", "sekiro_asset_manager", "ext_tools",
            "SekiroTAEExtractor", "SekiroTAEExtractor.exe",
        )

    # ---- 辅助路径 ----

    def get_python(self) -> str:
        """返回 UE5 自带的 Python 解释器路径。"""
        return os.path.join(
            self.engine_dir,
            "Engine", "Binaries", "ThirdParty", "Python3", "Win64", "python.exe",
        )

    def get_blender(self) -> str:
        """返回 Blender 可执行文件路径。

        探测顺序：
        1. settings.local.json 中 BLENDER_PATH 环境变量覆盖
        2. 常见安装路径列表
        3. 最后回退到 "blender"（依赖系统 PATH）
        """
        # 首先检查环境变量
        if env_val := self._env.get("BLENDER_PATH"):
            if os.path.exists(env_val):
                return env_val

        # 常见安装路径
        candidates = [
            r"C:\Program Files\Blender Foundation\Blender 4.2\blender.exe",
            r"C:\Program Files\Blender Foundation\Blender 3.6\blender.exe",
            r"C:\Program Files\Blender Foundation\Blender 4.1\blender.exe",
            r"C:\Program Files\Blender Foundation\Blender 4.0\blender.exe",
        ]
        for path in candidates:
            if os.path.exists(path):
                return path

        # 最后的回退：依赖系统 PATH
        return "blender"

    def get_bridge_py(self) -> str:
        """返回 AIBridge 的 bridge.py 路径。"""
        return os.path.join(
            self._project_dir, ".claude", "skills", "aibridge", "bridge.py",
        )

    # ---- Output 路径辅助 ----

    def output_model_dir(self, prefix: str) -> str:
        """返回 Output/<prefix>/Model/ 目录。"""
        return os.path.join(self.output_dir, prefix, "Model")

    def output_anim_dir(self, prefix: str) -> str:
        """返回 Output/<prefix>/Animation/ 目录。"""
        return os.path.join(self.output_dir, prefix, "Animation")

    def output_material_dir(self, prefix: str) -> str:
        """返回 Output/<prefix>/Material/ 目录。"""
        return os.path.join(self.output_dir, prefix, "Material")

    def output_model_json(self, prefix: str) -> str:
        """返回 Output/<prefix>/Model/<prefix>_model.json 路径。"""
        d = self.output_model_dir(prefix)
        return os.path.join(d, f"{prefix}_model.json")

    def output_anim_json(self, prefix: str) -> str:
        """返回 Output/<prefix>/Animation/<prefix>_common_anims.json 路径。"""
        d = self.output_anim_dir(prefix)
        return os.path.join(d, f"{prefix}_common_anims.json")

    def output_material_json(self, prefix: str) -> str:
        """返回 Output/<prefix>/Material/<prefix>_Materials.json 路径。"""
        d = self.output_material_dir(prefix)
        return os.path.join(d, f"{prefix}_Materials.json")


# 全局单例，方便其他模块直接 import 使用
config = PipelineConfig()