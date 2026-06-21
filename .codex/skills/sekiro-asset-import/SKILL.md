---
name: sekiro-asset-import
description: "Sekiro 资产导入管线：解包 → FLVER → JSON → UE Commandlet"
argument-hint: "<AssetName> [--original <OriginalAssetName>]"
user-invocable: true
allowed-tools: Bash, Read, Write, Edit, Glob, Grep, PowerShell, Agent
---

# Sekiro 资产导入

## 路径约定

| 含义 | 路径 |
|------|------|
| UE 引擎目录 | `$UE_ENGINE_DIR`（`.codex/settings.local.json` → `env.UE_ENGINE_DIR`） |
| Sekiro 游戏目录 | `$SEKIRO_GAME_DIR`（`.codex/settings.local.json` → `env.SEKIRO_GAME_DIR`） |
| 项目根目录 | `$PROJECT_DIR` |
| 解包数据 | `$SEKIRO_GAME_DIR/parts/<part>-partsbnd-dcx/` 或 `$PROJECT_DIR/Extracted/` |
| JSON 输出 | `$PROJECT_DIR/Output/<AssetName>/<AssetName>_model.json` |
| 纹理输出 | `Output/Textures/<OriginalAssetName>/`（资产专属）+ `Output/Textures/Shared/`（共享） |
| UE 导入路径 | `/Game/Characters/<AssetName>/`（可通过 `SK_UE_CONTENT_ROOT` 配置） |
| Python 管线 | `Script/sekiro_asset_manager/`（`python -m sekiro_asset_manager model import`） |
| C++ 插件 | `Plugins/SekiroAssetManager/`（编译后为 `SekiroAssetManager` 模块） |
| AIBridge | `.codex/skills/aibridge/bridge.py`（UE 编辑器 TCP JSON-RPC） |
| FlverToFbx | `Tools/FlverToFbx/`（.NET 9 工具，C# 实现） |
| Yabber | `Tools/Yabber 1.3.1/Yabber.exe`（FromSoft 文件解包） |

**严禁硬编码路径**。所有路径从 `PipelineConfig` 读取（优先级：`settings.local.json` > 环境变量 > 默认路径自动探测）。

## 目录结构

### Python 管线输出（磁盘）

```
$PROJECT_DIR/Output/
├── <AssetName>/                          # 用户指定的资产名
│   ├── <AssetName>_model.json            # 模型 JSON（骨骼、材质、纹理映射、ShaderType）
│   └── <AssetName>_anims.json            # 动画 JSON（如有）
└── Textures/
    ├── <OriginalAssetName>/              # 资产专属 PNG（来自 TPF DDS 转换）
    └── Shared/                           # 跨资产共享 PNG（从 Extracted/Textures/ 复制）
```

- `AssetName`：用户指定（如 `"test"`、`"Sekiro"`）
- `OriginalAssetName`：游戏原始标识（如 `"c0000"`），决定纹理子目录名
- `Output/Textures/` 是 UE Commandlet 导入时读取纹理的唯一来源

### UE Content Browser 结构（导入后）

```
{SK_UE_CONTENT_ROOT}/<AssetName>/
├── <AssetName>_Skeleton                  # 骨骼（USkeleton）
├── <AssetName>_Model                     # 主网格体（USkeletalMesh）
├── Materials/                            # 材质
│   ├── M_AM_M_9000_tops
│   ├── M_BD_M_9000_body
│   └── ...
├── Textures/                             # 纹理
│   ├── AM_M_9000_Armor_a
│   ├── AM_M_9000_Armor_n
│   └── ...
└── Animations/                           # 动画（全部放在一个文件夹下）
    ├── Anim_Sekiro_a000_200000
    ├── Anim_Sekiro_a000_200021
    └── ...
```

- `SK_UE_CONTENT_ROOT` 由用户配置（默认 `/Game/Characters`），可以是任意 UE 内容路径
- `AssetName` 也由用户指定（如角色叫 `Sekiro`，武器叫 `WP_A_0300`）
- 骨骼名 = `{AssetName}_Skeleton`，网格体名 = `{AssetName}_Model`
- 材质前缀 `M_`，纹理保留原始名
- 动画命名规则：`Anim_{AssetName}_{原始动画ID}`
  - 原始动画 ID 来自 HKX 文件名（如 `a000_200000`）
  - 示例：`Anim_Sekiro_a000_200000`、`Anim_Sekiro_a230_510000`
- 导入目标路径 = `{SK_UE_CONTENT_ROOT}/{AssetName}`，由 Python 管线自动拼合并传给 Commandlet 的 `-Output=`

## JSON 核心字段

### 顶层结构

| 字段 | 类型 | 说明 | 示例 |
|------|------|------|------|
| `FileName` | string | JSON 文件名 | `"test_model.json"` |
| `AssetName` | string | 用户指定的 UE 资产名 | `"test"` |
| `OriginalAssetName` | string | 游戏原始资产名（纹理子目录） | `"c0000"` |
| `SkeletonName` | string | 骨骼资产名（自动 = `{AssetName}_Skeleton`） | `"test_Skeleton"` |
| `Bones` | array | 排序后的骨骼数组（已过滤编辑器辅助骨骼） | `[...]` |
| `Meshes` | array | 网格体数组（含 `MaterialIndex`、`BoneIdxToName`、`Vertices`） | `[...]` |
| `Materials` | array | 材质数组（已合并 `ResolvedMaterials` 的纹理/混合模式） | `[...]` |
| `ResolvedMaterials` | array | 确定性纹理匹配结果（原始语义→PNG 映射） | `[...]` |
| `Dummies` | array | 虚拟体（Dummy Points） | `[...]` |
| `BoundingBox` | object | 包围盒 `{Min, Max}` | `{"Min":[0,0,0],"Max":[100,100,100]}` |

### Materials[] 中各字段

| 字段 | 说明 |
|------|------|
| `Name` | 材质名（重复时自动加 `Part` 前缀消歧） |
| `MTD` | MTD 文件相对路径 |
| `Part` | 所属部件名 |
| `Textures` | 语义 → PNG 文件名映射（从 `ResolvedMaterials` 合并） |
| `ResolvedBlendMode` | UE 混合模式：`"Opaque"` / `"Masked"` / `"Translucent"` |
| `TwoSided` | 是否双面渲染（`true`/`false`） |
| `IsCloth`、`IsHair`、`IsFur`、`IsDecal` | 材质类型标记 |
| `DrawStep` | 绘制顺序（`"Opaque"` / `"Masked"`） |

### ResolvedMaterials[] 中各字段

| 字段 | 说明 |
|------|------|
| `Name` | 原始材质名 |
| `Part` | 所属部件名 |
| `ShaderType` | 着色器枚举字符串（见下方 ShaderType 表） |
| `ShaderPath` | 原始 SPX 着色器文件名（如 `"Fur_NTC.spx"`） |
| `ResolvedBlendMode` | 确定的混合模式 |
| `TwoSided` | 双面标记 |
| `ClipValue` | 遮罩裁剪阈值（Masked 材质有值） |
| `IsCloth`、`IsHair`、`IsFur`、`IsDecal` | 材质类型标记 |
| `DrawStep` | 绘制顺序 |
| `Textures` | 语义 → PNG 文件名映射 |

## 确定性纹理匹配

**MTD 文件是唯一权威来源，不使用启发式回退。**

匹配流程：

```
FLVER 材质名 → 查找对应 MTD 文件（mtd_root/<basename>）
→ MtdParser.parse() 解析 MTD 二进制
→ 读取 MTD Textures[] 数组
→ 对每条记录：提取 Type（语义）、Path（tif 路径）
→ tif 文件名 → 在可用纹理列表中查找对应 PNG
→ 生成 {semantic: png_filename} 映射 → ResolvedMaterials JSON
→ build() 将结果合并到 Materials[].Textures → C++ 导入
```

### MTD Type → 语义映射

代码位置：`model_importer.py` `_build_resolved_materials()` 约行 640-680

| MTD Type 关键词 | 语义 | UE 材质引脚 |
|------------------|------|------------|
| `AlbedoMap`、`DiffuseMap` | `albedo` | BaseColor |
| `NormalMap` | `normal` | Normal |
| `MetallicMap`、`SpecularMap`、`ReflectanceMap` | `metallic` | ORM（金属度） |
| `RoughnessMap` | `roughness` | Roughness |
| `EmissiveMap` | `emissive` | Emissive Color |
| `AOMap`、`AmbientOcclusion`、`OccultusionMap` | `ao` | Ambient Occlusion |
| `OpacityMap`、`MaskMap`、`Mask1Map` | `opacityMask` | Opacity Mask |

未识别的 MTD Type 会被跳过，不做后缀猜测。

### 多条目支持（Multi-Entry）

当同一 MTD 中同一语义有多个槽位（如 2 个 AlbedoMap 分别指向 `armor_a.tif` 和 `armor_d.tif`）：

JSON 格式：
```json
{
  "albedo": "AM_M_9000_Armor_a.png",
  "albedo:1": "AM_M_9000_Armor_d.png",
  "normal": "AM_M_9000_Armor_n.png",
  "normal:1": "AM_M_9000_Armor_d_n.png"
}
```

优先规则（单条目时）：
- 非 damage 贴图 > damage 贴图
- head 贴图 > skin 贴图
- 其他同名贴图按多条目存储（`semantic:1`、`semantic:2`…）

### C++ 侧多条目混合（`SAMaterialImporter.cpp`）

| ShaderType | 语义 | 混合方式 | 实现 |
|------------|------|----------|------|
| `Standard` | `albedo` + `albedo:1` | Multiply（逐分量相乘） | `UMaterialExpressionMultiply` |
| `DetailBlend` / `DetailBlendCloth` | `albedo` + `albedo:1` | Multiply | 同上 |
| `Fur` / `FurCloth` | `normal` + `normal:1` | `BlendAngleCorrectedNormals` | `normalize(base×2-1 + detail×2-1) → (result+1)/2` |
| `Cloth` | `albedo` + `albedo:1` | Lerp（Mask1Map 驱动） | `UMaterialExpressionLinearInterpolate` |
| 其余 | 任意多条目 | Lerp（0.5 固定混合） | 同上 |

## 骨骼 / 网格体过滤

### drop_mesh_bone_prefixes

使用此参数删除不需要的骨骼及其蒙皮数据：

```python
ModelJsonBuilder.build(
    ...,
    drop_mesh_bone_prefixes=["HD_L_", "HD_R_"],  # 删除 HD 虚拟骨骼
)
```

自动处理（代码位置：`model_importer.py` 约行 391-520）：
- 删除骨骼名匹配前缀的网格体
- 从骨骼数组中移除未使用的骨骼（含父骨骼递归收集）
- 重新映射 `BoneIdxToName`（网格体 → 骨骼索引映射）
- 重新映射顶点 `BoneIndices`
- 重新映射骨骼 `ParentIndex`
- 过滤无用的材质（被删除网格体引用的材质）

### 编辑器辅助骨骼自动过滤

`ModelJsonBuilder._is_editor_bone()` 会自动过滤以下骨骼（约行 16-68）：

| 过滤条件 | 示例 |
|----------|------|
| 前缀 `Ctrl_` | `Ctrl_Spine`、`Ctrl_Head` |
| 前缀 `Collidable_` | `Collidable_Spine` |
| 前缀 `BD_Collidable_` | `BD_Collidable_Spine` |
| 精确名 `MoveSampling`、`TwistDummy`、`body_T`、`Box001` | — |
| 日语前缀：`オブジェクト`、`スカート`、`肩位置`、`位置反`、`回転反`、`抽`、`前後`、`胸部補` | — |

### 已知无 MTD 材质的兜底

`_FALLBACK_TEXTURE_MAP` 为极少数没有 MTD 文件的材质提供硬编码纹理映射（如 `BD_M_9000_body`、`BD_M_9000_tilingchain` 等，约行 28-35）。这些是游戏引擎内部材质，不在标准 MTD 目录中。

## 完整导入管线

### 1. 解包（Yabber）

```bash
"Tools/Yabber 1.3.1/Yabber.exe" "/parts/<file>.partsbnd.dcx"
"Tools/Yabber 1.3.1/Yabber.exe" "/parts/<part>-partsbnd-dcx/parts/<Type>/<Part>/<Part>.tpf"
"Tools/Yabber 1.3.1/Yabber.exe" "/parts/<part>-partsbnd-dcx/parts/<Type>/<Part>/<Part>.anibnd"
```

解包后文件位置：
- FLVER：`<partsbnd-dcx>/parts/<Type>/<Part>/<Part>.flver`
- DDS（TPF 内）：`<partsbnd-dcx>/parts/<Type>/<Part>/<Part>-tpf/`
- HKX：`<partsbnd-dcx>/parts/<Type>/<Part>/<Part>-anibnd/.../skeleton.hkx`
- 共享 PNG：`$PROJECT_DIR/Extracted/Textures/`

### 2. Python 管线（JSON + 纹理转换）

**CLI 方式：**
```bash
cd Script
python -m sekiro_asset_manager model import <AssetName> \
  --parts <body1.flver> <body2.flver> ... \
  --skeleton-flver <skeleton.flver> \
  --skeleton-hkx <skeleton.hkx>
```

**入口**：`Script/sekiro_asset_manager/__main__.py` → `cli.py:main()`

自动完成的步骤：
1. `generate_model_json()`：调用 `ModelJsonBuilder.build()` 生成 JSON
2. `_convert_tpf_dds_to_png()`：DDS → PNG 转换到 `Output/Textures/<OriginalAssetName>/`
3. 复制共享 PNG：`Extracted/Textures/*.png` → `Output/Textures/Shared/`
4. 可选：Blender FBX 导出（`_run_blender_fbx_export()`）
5. 可选：UE Commandlet 导入（`_run_ue_import()`）

**参数说明：**

| 参数 | 说明 |
|------|------|
| `asset_name` | 位置参数，用户指定的 UE 资产名 |
| `--parts` | 身体部件 FLVER 路径列表 |
| `--skeleton-flver` | 骨骼 FLVER 路径 |
| `--skeleton-hkx` | 骨骼 HKX 路径 |
| `--output-json` | 覆盖 JSON 输出路径 |
| `--output-fbx` | 覆盖 FBX 输出路径 |
| `--target-ue-path` | 覆盖 UE 导入路径（默认 `{SK_UE_CONTENT_ROOT}/{asset_name}`） |
| `--skip-unpack` | 跳过解包步骤 |
| `--skip-json-generation` | 跳过 JSON 生成 |
| `--skip-blender` | 跳过 Blender FBX 导出 |
| `--skip-ue-import` | 跳过 UE Commandlet 导入 |

**直接调用 ModelJsonBuilder.build()：**
```python
from sekiro_asset_manager.model_importer import ModelJsonBuilder

result = ModelJsonBuilder.build(
    flver_paths=[...],
    skeleton_flver="...",
    skeleton_source="...",      # HKX 路径或 JSON 路径
    mtd_root="...",
    tpf_roots=[...],
    asset_name="test",
    original_asset_name="c0000",
    drop_mesh_bone_prefixes=["HD_L_", "HD_R_"],  # 可选
    texture_lookup_dirs_extra=[...],              # 额外纹理查找目录
)
```

`build()` 返回值结构见上方「JSON 核心字段」。

### 3. UE Commandlet 导入

```bash
# 如果编辑器正在运行，先关闭（避免 Error 32：文件被占用）
"$UE_ENGINE_DIR/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$PROJECT_DIR/Sekiro.uproject" \
  -run=SAImportCommandlet \
  -Model="Output/<AssetName>/<AssetName>_model.json" \
  -Output="/Game/Characters/<AssetName>" \
  -unattended
```

`-Output=` 必传。Python 管线自动从 `target_ue_path` 传入。

**C++ 导入逻辑**（`Plugins/SekiroAssetManager/`）：
1. `SAImportCommandlet.cpp`：解析 `-Model=` JSON，读取 `-Output=` 目标路径
2. `SAModelImporter.cpp`：解析 `AssetName`、`OriginalAssetName`、`SkeletonName`，构建 `USkeleton` + `USkeletalMesh`
3. 纹理搜索：`Output/Textures/{OriginalAssetName}/` + `Output/Textures/Shared/`
4. `SAMaterialImporter.cpp`：根据 `ShaderType` 创建材质表达式图、处理多条目混合、连接材质引脚

导入生成：
- `USkeleton` → `{Output}/{SkeletonName}`
- `USkeletalMesh` → `{Output}/{SkeletonName}_Model`（去掉 `_Skeleton` 后缀 + `_Model`）
- 所有 `UMaterial` → `{Output}/Materials/{MatName}`
- 所有 PNG 纹理 → `{Output}/Textures/{tex_name}`

### 4. 蓝图创建

```bash
# 启动 AIBridge（连接运行的 UE 编辑器）
python .codex/skills/aibridge/bridge.py editor start

# 创建蓝图（基于 ASKCharacter）
python .codex/skills/aibridge/bridge.py blueprint create \
  /Game/Characters/<AssetName>/BP_<AssetName> "/Script/Sekiro.ASKCharacter"

# 编译蓝图
python .codex/skills/aibridge/bridge.py blueprint compile \
  /Game/Characters/<AssetName>/BP_<AssetName>
```

AIBridge 通过 TCP JSON-RPC（默认 `localhost:9876`）操控 UE 编辑器。

## 游戏部件文件查找

| 类型 | 游戏 Bundled 文件 | 解包后目录模式 |
|------|------------------|---------------|
| 角色 | `chr/c0000.chrbnd.dcx` | 完整导入（FLVER + HKX + TPF 均在同级） |
| 武器 | `parts/wp_a_<id>.partsbnd.dcx` | `parts/Weapon/WP_A_<id>/` |
| 盔甲 | `parts/<part>_m_<id>.partsbnd.dcx` | `parts/FullBody/<PART>_M_<id>/` |
| 脸部 | `parts/fc_m_<id>.partsbnd` | `parts/Face/FC_M_<id>/` |

## ShaderType 枚举

### JSON ↔ C++ 映射

代码位置：
- Python：`model_importer.py` `SHADER_TYPE_MAP`（约行 40-55）
- C++：`SAImportData.h` `ESekiroShaderType`（约行 61-73）

| JSON ShaderType | C++ ESekiroShaderType | 匹配的 SPX 着色器 | 说明 |
|-----------------|----------------------|-------------------|------|
| `Standard` | `Standard` | `character_amsn.spx` | 标准 PBR |
| `SSS` | `SSS` | `character_amsn_sss.spx` | 次表面散射（皮肤） |
| `Fur` | `Fur` | `fur_ntc.spx` | 毛发（Masked + 双面） |
| `DetailBlend` | `DetailBlend` | `character_amsn_[detailblend].spx` | 布料细纹混合 |
| `FurCloth` | `FurCloth` | `fur_ntc_cloth.spx` | 毛发布料 |
| `DetailBlendCloth` | `DetailBlendCloth` | `character_amsn_[detailblend]_cloth.spx` | 布料细纹变体 |
| `FresnelBlend` | `FresnelBlend` | `character_an_blend_amsn_[fresnel].spx` | 菲涅尔混合 |
| `FresnelBlendCloth` | `FresnelBlendCloth` | `character_an_blend_amsn_[fresnel]_cloth.spx` | 菲涅尔布料 |
| `SSSCloth` | `SSSCloth` | `character_amsn_e_sss_cloth.spx` | SSS 布料 |
| `Skin` | `Skin` | `character_amsn_[ao_sss].spx` | 皮肤 |
| `Eye` | `Eye` | `character_amsn_[cat_eye].spx` | 眼球 |
| `Cloth` | `Cloth` | `character_amsn_cloth.spx` | 布料（Masked + 双面） |

### C++ 侧 ShaderType 行为差异

代码位置：`SAMaterialImporter.cpp` 约行 290-310

| ShaderType | BlendMode | PBR | 特殊处理 |
|------------|-----------|-----|----------|
| `Fur` / `FurCloth` | Masked | 关闭 | `Metallic=0`，`Roughness=0.6` |
| `Cloth` / `DetailBlendCloth` | Masked | 开启 | `Roughness=0.5` |
| `Eye` | Opaque | 开启 | 仅连接 albedo+normal，跳过 ORM |
| 其余 | 按 `ResolvedBlendMode` | 开启 | 标准 PBR 流程 |

### BlendMode 判定（Python 侧）

代码位置：`model_importer.py` `_build_resolved_materials()` 约行 720-760

- `ShaderPath` 含 `"sss"` → Opaque（皮肤不走 Masked）
- `ShaderPath` 含 `"fur"`（且非 skin）→ Masked，`ClipValue=0.25`（hair 为 0.33）
- MTD 文件名含 `"cloth"`（且非 skin）→ Masked，`ClipValue=0.5`
- MTD 文件名含 `"decal"` → Masked，`ClipValue=0.25`
- MTD 文件名含 `"_blend"` → Translucent
- 其余 → 从 MTD `BlendMode` 映射：`Normal→Opaque`、`TexEdge→Masked`、`Blend→Translucent`

## 配置

| 配置项 | 位置 | 默认值 | 说明 |
|--------|------|--------|------|
| `UE_ENGINE_DIR` | `settings.local.json` → `env` | 自动探测 | UE5 引擎目录 |
| `SEKIRO_GAME_DIR` | `settings.local.json` → `env` | 自动探测 | Sekiro 安装目录 |
| `SK_UE_CONTENT_ROOT` | `settings.local.json` → `env` | `/Game/Characters` | UE Content Browser 根路径 |
| `BLENDER_PATH` | `settings.local.json` → `env` | 自动探测 | Blender 可执行文件路径 |

`PipelineConfig`（`pipeline_config.py`）统一管理所有路径，属性包括：
`project_dir`、`tools_dir`、`extracted_dir`、`output_dir`、`content_dir`、`ue_content_root`、`scripts_dir`、`game_dir`、`engine_dir`

## 规则

- **禁止手动编辑 JSON**：Python `ModelJsonBuilder.build()` 和 `_build_resolved_materials()` 处理一切
- **禁止手动转换纹理**：`_convert_tpf_dds_to_png()` 自动完成 DDS → PNG
- **禁止手动设置 SkeletonName**：Python 自动设为 `{AssetName}_Skeleton`，C++ 从 JSON 读取
- **禁止 C++ 硬编码路径**：`-Output=` 由 Python 传入，纹理从 `Output/Textures/` 读取
- **纹理匹配仅使用 MTD**：零启发式回退，100% 确定性（`_FALLBACK_TEXTURE_MAP` 仅用于无 MTD 的引擎内部材质）
- **导入前关闭 UE 编辑器**：运行中的 UE 编辑器会锁定资产文件（Error 32）
- **删除旧资产再导入**：`-Output=` 指向的目录需先清空，避免残留旧版本资产
- **Python 管线不直接操作 UE 编辑器**：通过 JSON + Commandlet 传递数据，AIBridge 仅用于蓝图创建






