# Sekiro 材质管线完整修复方案

> 状态：进行中 | 日期：2026-06-05

## 1. 工作目的

修复从 Sekiro 游戏文件解包到 UE5 导入的整条材质管线，解决三类问题：
- **贴图乱贴**：部分材质显示错误纹理
- **半透明/混合模式不对**：布料、毛发、贴花等应半透明/镂空的材质显示为不透明
- **质感偏差**：金属度、粗糙度等参数不符合游戏内效果

## 2. 管线架构与数据流

```
Sekiro 游戏文件 (.partsbnd.dcx, .chrbnd.dcx, .anibnd.dcx)
    │
    ▼  [Yabber.exe 解包]
Extracted/ 目录树
    ├── bd_m_9000-partsbnd-dcx/parts/FullBody/BD_M_9000/
    │   ├── BD_M_9000.flver      ← 模型、材质、骨骼
    │   ├── BD_M_9000.tpf        ← 纹理包
    │   └── BD_M_9000-tpf/       ← TPF 解包后的 DDS 纹理
    │
    ▼  [FlverToFbx.exe (C# Program.cs + SoulsFormats)]
Sekiro_model_hkx.json  ← 骨骼层级 + 网格数据 + 材质信息(含 MTD 路径 + 纹理引用)
    │
    ▼  [Blender Python (common_blender.py)]
    ├── 纹理匹配：文件名启发式 → Blender 材质节点
    ├── 混合模式判断：MTD 路径字符串匹配
    ├── Sekiro_Model.fbx           ← 最终模型
    └── Sekiro_Materials.json      ← UE5 材质配置
    │
    ▼  [UE5 Python (ue5_import_character.py) + C++ 插件 (SekiroMaterialUtils)]
/Game/Sekiro/Wolf/
    ├── Materials/  ← 材质 + 纹理
    └── Models/     ← SkeletalMesh + PhysicsAsset + Skeleton
```

## 3. 根因分析

### 3.1 C# 解包丢弃了所有 FLVER 纹理参数名（根因 1）

**问题**：FLVER 文件中 BD_M_9000 部分有 136 个纹理条目，每个都有 `tex.ParamName`（着色器参数名，如 `Character_AMSN__DetailBlend__snp_Texture2D_7_AlbedoMap`），但 `tex.Path`（纹理文件路径）全部为空字符串。

`Program.cs` 第 306 行：
```csharp
if (!string.IsNullOrEmpty(tex.Path))  // ← 跳过了所有 136 个纹理！
```

这导致输出的 JSON 中所有材质的 `Textures` 字段都是空 `{}`。**ParamName 中包含了纹理语义信息**（AlbedoMap=反照率, NormalMap=法线, SpecularMap=高光），这些全部丢失。

**原理**：Sekiro 的 FLVER 格式中，`Material.Textures[].Type` 存储纹理**槽位名**（如 `g_DiffuseTexture`），`Material.Textures[].ParamName` 存储着色器**参数名**（如 `Character_AMSN__DetailBlend__snp_Texture2D_7_AlbedoMap`），而实际纹理文件路径在 MTD 文件或 TPF 包中通过命名约定关联。C# 代码只检查了 `Path` 字段，导致 Type 和 ParamName 信息全部丢失。

### 3.2 Blender 与 JSON 的 Blend Mode 逻辑不一致（根因 2）

**问题**：两个地方判断混合模式，用了不同的标准：

| 位置 | 判断依据 | 关键词范围 |
|------|---------|-----------|
| `common_blender.py` 第 481 行（Blender 材质）| 材质名称 | CLOTH_KEYWORDS（含 fray, tiling, bandage, muffler, rope 等） |
| `common_blender.py` 第 519 行（JSON 配置）| MTD 文件名 | 仅 `'cloth'` |

**Bug 后果**：
- `BD_M_9000_fray1`：名称含 "fray"，MTD 不含 "cloth" → Blender 中 CLIP/Masked，**UE5 中 Opaque**
- `BD_M_9000_tilingchain`、`BD_M_9000_tilingrope`、`LG_M_9000_tilingbandage` 同理

### 3.3 MTD 文件未提取/未解析（根因 3）

**问题**：SoulsFormats 库完整支持 `SoulsFormats.MTD` 类，可解析 MTD 二进制文件获取：
- **纹理槽位定义**：`MTD.Textures[].Type` = `g_DiffuseTexture`, `g_SpecularTexture` 等
- **BlendMode 枚举**：Normal(0), Blend(2), Add(4), Mul(6), TexEdge(1) 等 16 种
- **LightingType 枚举**：None(0), HemDirDifSpcx3(1), HemEnvDifSpc(2)
- **材质参数**：`g_Metallic`, `g_Roughness`, `g_SpecularPower` 等 float 值

但当前解包流程（Yabber）不提取 .mtd 文件，Python 端也只把 MTD 路径当字符串做关键字匹配。

### 3.4 纹理分配纯靠启发式模糊匹配（根因 4）

`score_texture_candidate()` 函数根据文件名与材质名的相似度打分，完全不使用 FLVER 中真实的 ParamName→语义映射。例如：
- `BD_M_9000_body`（身体材质）被分配了 `BD_M_9000_cloth_a.png`（布料纹理）
- 多个不同材质共享同一张纹理（因分数匹配机制）

## 4. 修复方案

### Phase 1：C# 端 — 捕获纹理元数据 + MTD 解析

**文件**：`Tools/FlverToFbx/FlverToFbx/Program.cs`

**改动 1**：不再丢弃 Path 为空的纹理，保存 `Type` 和 `ParamName` 为结构化对象：

```csharp
// 每个纹理条目输出为:
{
    "type": "g_DiffuseTexture",           // FLVER2.Texture.Type
    "paramName": "Character_AMSN__...AlbedoMap",  // FLVER2.Texture.ParamName
    "path": ""                             // FLVER2.Texture.Path（Sekiro中为空）
}
```

**改动 2**：尝试加载对应的 MTD 文件并解析：

```csharp
// MTD 路径转换: N:\NTC\data\Material\mtd\parts\P_BD_M_9000_tops1.mtd
//            → Extracted/mtd/parts/P_BD_M_9000_tops1.mtd
var mtd = MTD.Read(localMtdPath);
// 输出: { shaderPath, blendMode, lightingType, params[], textureSlots[] }
```

**改动 3**：收集每个 part 的 TPF 解包目录中的可用纹理文件列表。

**改动 4**：新增 `inspect_flver_materials.csx` 调试脚本。

### Phase 2：Python 端 — 修复 Blend Mode + 语义纹理映射

**文件**：`Tools/FlverToFbx/common_blender.py`

**改动 1**：修复 Blend Mode BUG — JSON 配置生成统一使用 CLOTH_KEYWORDS：

```python
# 修改前：只用 MTD 名中的 'cloth'
is_cloth = 'cloth' in mtd

# 修改后：同时检查材质名称
is_cloth_mat = any(kw in mat_low for kw in CLOTH_KEYWORDS) or ('cloth' in mtd)
```

**改动 2**：新增 ParamName→纹理语义映射：

```
AlbedoMap/DiffuseMap/BaseColorMap → _a (反照率)
NormalMap/Bumpmap/DetailBumpmap    → _n (法线)
SpecularMap/MetallicMap            → _m (金属度/高光)
RoughnessMap/ShininessMap          → _r (粗糙度)
AmbientOcclusionMap                → _ao (AO)
DisplacementMap                    → _d (置换)
EmissiveMap                        → _em (自发光)
BloodMask                          → _1m (蒙版)
```

**改动 3**：优先用 ParamName 匹配，降级到现有启发式。

**改动 4**：如果 MTD 解析可用，用真实 BlendMode 覆盖启发式。

### Phase 3：MTD 文件提取

**新增文件**：`Tools/FlverToFbx/extract_mtd.py`

MTD 文件不在 partsbnd 包中，需要在游戏数据目录中寻找。如无法提取，Phase 2 的 ParamName 降级方案可独立工作。

### Phase 4：UE5 端适配

**文件**：
- `Tools/FlverToFbx/ue5_import_character.py` — 支持 MTD BlendMode 映射
- `Plugins/SekiroTools/.../SekiroMaterialUtils.cpp` — 增强采样器修复覆盖 Specular

## 5. 实施顺序

| 序号 | 任务 | 影响 | 复杂度 |
|------|------|------|--------|
| 1 | C# 端保存 ParamName + Type | 数据源修复 | 中 |
| 2 | 修复 Blend Mode BUG | 最大用户体验影响 | 低 |
| 3 | ParamName→语义纹理映射 | 解决贴图乱贴 | 中 |
| 4 | MTD 文件提取 + 解析 | 质感 + 参数准确 | 高 |
| 5 | UE5 端适配 | 配合上述改动 | 低 |

## 6. 关键文件

| 文件 | 改动 |
|------|------|
| `Tools/FlverToFbx/FlverToFbx/Program.cs` | 纹理提取 + MTD 解析 |
| `Tools/FlverToFbx/common_blender.py` | 纹理映射 + blend mode 修复 |
| `Tools/FlverToFbx/ue5_import_character.py` | MTD blend mode 支持 |
| `Plugins/SekiroTools/Source/SekiroTools/Private/SekiroMaterialUtils.cpp` | 采样器增强 |
| `Tools/FlverToFbx/extract_mtd.py` | 新增：MTD 提取 |
| `Tools/FlverToFbx/FlverToFbx/inspect_flver_materials.csx` | 新增：调试脚本 |

## 7. 纹理后缀与 UE5 压缩配置对照

| 后缀 | 语义 | sRGB | 压缩 | 采样器 | 材质槽 |
|------|------|------|------|--------|--------|
| `_a` | 反照率/漫反射 | true | Default | Color | Base Color + Alpha |
| `_n` | 法线 | false | Normalmap | Normal | Normal |
| `_m` | 金属度/高光 | false | Grayscale | Grayscale | Metallic / Specular |
| `_r` | 粗糙度 | false | Grayscale | Grayscale | Roughness |
| `_ao` | 环境光遮蔽 | false | Grayscale | Grayscale | AO |
| `_d` | 置换 | false | Default | Linear | Displacement |
| `_em` | 自发光 | false | Default | Linear | Emissive |
| `_1m` | 蒙版 | false | Grayscale | Grayscale | Opacity Mask |

## 8. MTD BlendMode → UE5 映射

| MTD 值 | 数值 | UE5 BlendMode | 说明 |
|--------|------|---------------|------|
| Normal | 0 | BLEND_OPAQUE | 标准不透明 |
| TexEdge | 1 | BLEND_MASKED | Alpha 裁剪 |
| Blend | 2 | BLEND_TRANSLUCENT | Alpha 混合 |
| Water | 3 | BLEND_TRANSLUCENT | 水面 |
| Add | 4 | BLEND_ADDITIVE | 叠加 |
| Sub | 5 | BLEND_MODULATE | 减法 |
| Mul | 6 | BLEND_MODULATE | 乘法 |
| LS* 前缀 | 32-41 | 同上 | 光照空间变体 |
