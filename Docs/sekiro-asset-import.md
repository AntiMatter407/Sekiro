# Sekiro 资产导入管线

## 工具

所有工具统一放在 `Script/sekiro_asset_manager/ext_tools/`：

| 工具 | 类型 | 用途 |
|------|------|------|
| `Yabber/` | 外部 | 解包 .dcx/.tpf/.anibnd/.parambnd |
| `FlverToJson/` | 外部 | FLVER → JSON 模型提取 |
| `texconv/` | 外部 | DDS → PNG 转换 |
| `SekiroAnimExtractor/` | 外部 | HKX 动画 → JSON 提取 |
| `SekiroTAEExtractor/` | C# | .tae 二进制 → JSON 事件数据 |
| `ParamReader/` | C# | BehaviorParam_PC.param → JSON 行为配置 |
| `SoulsAssetPipeline/` | 共享库 | SoulsFormats.dll（FLVER/HKX/PARAM 解析） |

> 工具由 `pipeline_config.py` 的 `DEFAULT_TOOLS` 自动定位，路径可配。

## 快速流程

```bash
# 1. 解包
Yabber.exe <game_dir>/chr/c0000.chrbnd.dcx
Yabber.exe <game_dir>/parts/<部件>.partsbnd.dcx
Yabber.exe <解包目录>/<部件>.tpf
Yabber.exe <解包目录>/<部件>.anibnd

# 2. FLVER → JSON
python -m sekiro_asset_manager model import <AssetName> --original <游戏原始名>

# 3. DDS → PNG（在 model_importer 中自动完成）

# 4. TAE 事件提取
dotnet run --project Script/sekiro_asset_manager/ext_tools/SekiroTAEExtractor -c Release -- <tae_dir> Output/Sekiro_TAE_Logic.json

# 5. BehaviorParam 提取
dotnet run --project Script/sekiro_asset_manager/ext_tools/ParamReader -c Release -- <BehaviorParam_PC.param路径> Output/

# 6. 导入 UE
UnrealEditor-Cmd.exe Sekiro.uproject -run=SekiroImport \
  -Model="Output/<AssetName>/<AssetName>_model.json"

# 7. 创建蓝图（需要 UE 编辑器运行中）
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
| `Output/` | JSON 输出：模型、动画、TAE 事件、BehaviorParam（不入 git） |
| `Content/` | UE 资源（.uasset 不入 git，LFS 管理） |
| `Tools/` | 外部工具备份（不入 git） |

## Output 文件清单

| 文件 | 来源 | 说明 |
|------|------|------|
| `BehaviorParam_PC.json` | ParamReader | 611 条 BehaviorParam，49 个 variation_id |
| `Sekiro_TAE_Logic.json` | SekiroTAEExtractor | TAE 事件数据（JT 跳转表 + 攻击判定 + 取消窗口） |
| `*_model.json` | FlverToJson | 模型 JSON（顶点/骨骼/材质） |
| `*_anims_*.json` | SekiroAnimExtractor | 动画曲线 JSON |

动画导入会同时读取同一 `anibnd` 内的 TAE MiniHeader。`ImportHKX` 会复用来源动作曲线并保留
逻辑动画自身事件；`ImportOtherAnim` 会继承来源动作与事件。管线在动画 JSON 阶段物化逻辑别名，
因此原包没有独立 HKX 的动画编号仍可生成对应 UE `UAnimSequence`。

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
- BehaviorParam ↔ AnimID 映射 → `Docs/design/sekiro-anim-state-machine-extraction.md`
