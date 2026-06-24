# Sekiro 资产导入管线

## 工具

所有工具统一放在 `Script/sekiro_asset_manager/ext_tools/`：

| 工具 | 用途 |
|------|------|
| `Yabber.exe` | 解包 .dcx/.tpf/.anibnd |
| `FlverToJson.exe` | FLVER → JSON 提取 |
| `texconv.exe` | DDS → PNG 转换 |
| `SekiroAnimExtractor.exe` | 动画提取 |

> 工具由 `pipeline_config.py` 的 `DEFAULT_TOOLS` 自动定位，路径可配。

## 快速流程

```bash
# 1. 解包
Yabber.exe <game_dir>/parts/<部件>.partsbnd.dcx
Yabber.exe <解包目录>/<部件>.tpf
Yabber.exe <解包目录>/<部件>.anibnd

# 2. FLVER → JSON
python -m sekiro_asset_manager model import <AssetName> --original <游戏原始名>

# 3. DDS → PNG（在 model_importer 中自动完成）

# 4. 导入 UE
UnrealEditor-Cmd.exe Sekiro.uproject -run=SekiroImport \
  -Model="Output/<AssetName>/<AssetName>_model.json"

# 5. 创建蓝图（需要 UE 编辑器运行中）
python .codex/skills/aibridge/bridge.py blueprint create \
  /Game/Characters/<AssetName>/BP_<AssetName> \
  /Script/Sekiro.ASKCharacter
```

## 关键路径

| 路径 | 说明 |
|------|------|
| `$SEKIRO_GAME_DIR` | Sekiro 安装目录（.codex/settings.local.json） |
| `$UE_ENGINE_DIR` | UE 5.2 引擎目录（.codex/settings.local.json） |
| `Extracted/` | 解包中间文件（不入 git） |
| `Output/<AssetName>/` | JSON + 动画输出（不入 git） |
| `Content/` | UE 资源（.uasset 不入 git，LFS 管理） |
| `Tools/` | 外部工具备份（不入 git） |

## 配置

`pipeline_config.py` 从以下来源合并配置（优先级递减）：
1. `.codex/settings.local.json` / `.claude/settings.local.json`
2. 环境变量
3. 默认路径自动探测

## 详细文档

- 完整管线说明 → `Docs/sekiro-asset-pipeline.md`
- 材质/纹理 → `Docs/pipeline-materials-unified.md`
- 动画系统 → `Docs/sekiro-animation-system.md`
- 武器纹理 → `Docs/pipeline-weapon-textures.md`