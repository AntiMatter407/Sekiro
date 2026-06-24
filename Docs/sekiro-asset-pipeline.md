# 只狼资源导入管线

## 概述

将 FromSoftware《Sekiro: Shadows Die Twice》的角色资源（模型、骨骼、材质、动画、行为参数、TAE 事件）从游戏私有二进制格式转换为结构化 JSON 中间格式。分为三个阶段：

- **Phase 1** — 资源提取：从游戏目录解包 .dcx 压缩包，得到 .flver / .hkx / .tpf / .mtd / .tae / .param
- **Phase 2** — JSON 生成：C# 工具将二进制格式转为结构化 JSON
- **Phase 3** — UE5 导入：JSON 通过 Commandlet 导入为 .uasset

---

## Phase 1：资源提取

### 工具

| 工具 | 路径 | 用途 |
|------|------|------|
| Yabber 1.3.1 | `Tools/Yabber 1.3.1/Yabber.exe` | 万能解包器：BND4/DCX/TPF/PARAMBND |
| unpack_sekiro.py | `Script/unpack_sekiro.py` | 批量提取脚本 |
| extract_mtd.py | `Script/extract_mtd.py` | 提取 MTD 材质定义 |

### 源文件格式

| 扩展名 | 格式 | 内容 |
|--------|------|------|
| `.chrbnd.dcx` | DCX→BND4 包 | `c0000.flver`(骨骼) + `skeleton.hkx` |
| `.partsbnd.dcx` | DCX→BND4 包 | `*.flver`(模型) + `*.tpf`(纹理) |
| `.anibnd.dcx` | DCX→BND4 包 | 多个 `*.hkx` 动画文件 + `*.tae` 事件文件 |
| `.parambnd.dcx` | DCX→BND4 包 | `*.param` 参数表（BehaviorParam / AtkParam 等） |
| `.mtdbnd.dcx` | DCX→BND4 包 | `*.mtd` 材质定义 |
| `.flver` | FLVER2 | 骨骼节点、网格、材质引用、纹理槽 |
| `.hkx` | Havok TAG0 | 骨骼层次+参考姿势，或动画曲线 |
| `.tpf` | Texture Pack | 多个 `.dds` 纹理 |
| `.tae` | TAE 二进制 | 动画事件（攻击判定框、取消窗口、特效） |
| `.param` | PARAM (Souls) | 游戏参数表（行为配置、攻击参数等） |
| `.mtd` | Material Definition | Shader路径、混合模式、纹理类型槽 |

### 提取流程

```
Sekiro Game Directory
  ├── chr/c0000.chrbnd.dcx    → Yabber → Extracted/c0000-chrbnd-dcx/
  │                              ├── c0000.flver          (骨骼 FLVER)
  │                              └── skeleton.hkx         (HKX 骨骼)
  ├── chr/c0000_*.anibnd.dcx  → Yabber → Extracted/c0000_*-anibnd-dcx/
  │                              ├── a00_*.hkx            (动画文件)
  │                              └── tae/*.tae            (TAE 事件)
  ├── parts/hd_m_9520.partsbnd.dcx → Yabber →
  │                              ├── hd_m_9520.flver      (脸部模型)
  │                              └── hd_m_9520-tpf/       (脸部纹理 .dds)
  ├── parts/bd_m_9000.partsbnd.dcx → Yabber → ...
  ├── Data/gameparam.parambnd.dcx  → Yabber → Extracted/gameparam/
  │                              └── param/GameParam/
  │                                  ├── BehaviorParam_PC.param
  │                                  ├── AtkParam_PC.param
  │                                  └── ... (其他 param 表)
  └── Data/Data*.mtdbnd.dcx   → Yabber → Extracted/mtd/
                                 └── *.mtd                (材质定义)
```

---

## Phase 2：JSON 生成 (C# 工具)

### 工具链

所有 C# 工具位于 `Script/sekiro_asset_manager/ext_tools/`：

| 项目 | 路径 | 输出 | 格式 |
|------|------|------|------|
| FlverToJson | `ext_tools/FlverToJson/` | 模型 JSON | FLVER2 → JSON |
| SekiroAnimExtractor | `ext_tools/SekiroAnimExtractor/` | 动画 JSON | HKX → JSON |
| SekiroTAEExtractor | `ext_tools/SekiroTAEExtractor/` | TAE 事件 JSON | .tae → JSON |
| ParamReader | `ext_tools/ParamReader/` | BehaviorParam JSON | .param → JSON |
| SoulsAssetPipeline | `ext_tools/SoulsAssetPipeline/` | 共享库 DLL | SoulsFormats.dll |

> 运行方式：`dotnet run --project <项目路径> -c Release -- <参数>`
> 共享 `ext_tools/SoulsFormats.dll`（从 DSAnimStudio 编译输出复制）

### Step A：生成模型 JSON

```bash
FlverToJson.exe \
  c0000.flver \           # 骨骼 FLVER
  hd_m_9520.flver \      # 脸部
  bd_m_9000.flver \      # 身体
  am_m_9000.flver \      # 手臂
  lg_m_9000.flver \      # 腿
  --skeleton-hkx skeleton.hkx \
  -o Sekiro_model_hkx.json
```

**内部流程：**

1. 读取每个 `.flver` 的 `FLVER2` 结构：Nodes(骨骼节点)、Meshes(网格)、Materials(材质槽)、Textures(纹理引用)
2. 读取 HKX 骨骼获取四元数参考姿势（优先），否则回退到 FLVER Euler 角 (XZY 顺序)
3. 递归计算每个骨骼的世界变换：`worldMat[i] = localMat[i] * worldMat[parent]`
4. 解析每个材质的 MTD 文件，获取纹理槽类型→贴图映射
5. 合并所有网格顶点数据（Position/Normal/BoneIndices/BoneWeights/UV）
6. 输出 JSON

**输出：`Sekiro_model_hkx.json`** (~135 MB)

```json
{
  "SkeletonName": "c0000",
  "BoneCount": 148,
  "Bones": [{
    "Name": "Master",
    "ParentName": "",
    "LocalPos": [0, 0, 0],
    "LocalRot": [0, 0, 0],
    "LocalScale": [1, 1, 1],
    "WorldPos": [0, 1.2, 0],
    "WorldRot": [0, 0, 0, 1]
  }],
  "MaterialCount": 12,
  "Materials": [{
    "Name": "BD_M_9000",
    "MTD": "mtd/BD_M_9000.mtd",
    "Part": "Body",
    "Textures": [{
      "ParamName": "g_DiffuseTexture",
      "Path": "HD_M_9000_a.dds",
      "TilingScale": [1, 1]
    }]
  }],
  "MeshCount": 5,
  "Meshes": [{
    "Part": "Body",
    "MaterialIndex": 0,
    "Vertices": [{
      "Pos": [0, 1.0, 0.2],
      "Normal": [0, 1, 0],
      "BoneIndices": [0, 1, 0, 0],
      "BoneWeights": [0.6, 0.4, 0, 0],
      "UV": [0.5, 0.3]
    }],
    "Triangles": [[0, 1, 2], [0, 2, 3]],
    "BoneIdxToName": { "0": "Master", "1": "Spine1" }
  }]
}
```

### Step B：生成动画 JSON

```bash
SekiroAnimExtractor.exe skeleton.hkx anim_dir/ Sekiro_animations.json --sample-rate 30
```

**内部流程：**

1. 解析 HKX 骨骼：BoneNames, BoneParents, 本地参考姿势 (四元数)
2. 遍历目录下所有 `.hkx` 动画文件
3. 采样动画曲线：支持 `SplineCompressed` 和 `InterleavedUncompressed` 两种 Havok 格式
4. 每帧提取每条骨骼的 Translation(米) / Rotation(四元数 xyzw) / Scale
5. 输出 JSON

**输出：`Sekiro_animations.json`**（完整动画集 ~1 GB；精简版 `Sekiro_common_anims.json` ~3.5 MB 只含 idle/walk/run/sprint）

```json
{
  "BoneCount": 148,
  "BoneNames": ["Master", "Spine1", ...],
  "BoneParents": [-1, 0, ...],
  "BoneLocalTransforms": [{
    "P": [0, 1.2, 0],
    "R": [0, 0, 0, 1],
    "S": [1, 1, 1]
  }],
  "AnimationCount": 450,
  "Animations": [{
    "Name": "a00_3000",
    "FrameCount": 30,
    "Frames": [{
      "BoneTransforms": [{
        "P": [0, 1.2, 0],
        "R": [0, 0, 0, 1],
        "S": [1, 1, 1]
      }]
    }]
  }]
}
```

### Step C：生成 TAE 事件 JSON

```bash
dotnet run --project Script/sekiro_asset_manager/ext_tools/SekiroTAEExtractor -c Release -- \
  Extracted/c0000-anibnd-dcx/chr/c0000/tae/ \
  Output/Sekiro_TAE_Logic.json
```

**内部流程：**

1. 解析 `TAE.Template.SDT.xml` 获取事件类型定义
2. 遍历 tae 目录下所有 `.tae` 文件
3. 按模板解析每个事件的二进制布局（JumpTableID / Type / 参数）
4. 提取关键事件类型：
   - Type=1：攻击判定框（BehaviorJudgeID + 判定框尺寸）
   - Type=2：子弹/投射物
   - Type=5：特殊攻击
   - JT=25/26/115/117/118/154：取消窗口
   - JT=7/51/89：帧标志（NoTurn/Invincible/NoMove）
5. 输出 JSON

**输出：`Sekiro_TAE_Logic.json`** (~28 MB，21148 个 JT 事件)

```json
{
  "anims": {
    "300000": {
      "anim_id": 300000,
      "events": [
        {
          "type": 1,
          "judge_id": 0,
          "start_frame": 5,
          "end_frame": 12,
          "hitbox": { ... }
        },
        {
          "jt_id": 25,
          "jt_name": "DodgeCancelStart",
          "start_frame": 0,
          "end_frame": 10
        }
      ]
    }
  }
}
```

### Step D：生成 BehaviorParam JSON

```bash
dotnet run --project Script/sekiro_asset_manager/ext_tools/ParamReader -c Release -- \
  Extracted/gameparam/gameparam-parambnd-dcx/param/GameParam/BehaviorParam_PC.param \
  Output/
```

**内部流程：**

1. SoulsFormats `PARAM.Read()` 解析 .param 标头和行索引
2. 反射访问 `Row.DataOffset`（internal 字段）获取每行数据偏移
3. 按 BehaviorParam 布局（30 字节/行）解析：
   - variation_id (= AnimID / 100)
   - behaviorJudgeID（链接 TAE 事件）
   - RefType（0=AtkParam, 1=Bullet, 2=SpEffect）
   - RefID（引用目标 ID）
   - stamina / mp / category / hero_point
4. 输出 JSON

**输出：`BehaviorParam_PC.json`** (170 KB，611 条)

```json
[
  {
    "variation_id": 5000,
    "row_id": 105000000,
    "judge_id": 0,
    "ez_state_behavior_type": 0,
    "ref_type": 0,
    "ref_type_name": "Attack",
    "ref_id": 5000000,
    "sfx_id": 0,
    "stamina": 0,
    "mp": 0,
    "category": 1,
    "hero_point": 0
  }
]
```

**编码公式（已验证）：**

```
Row.ID  = 100000000 + variation_id × 1000 + behaviorJudgeID
AnimID  = variation_id × 100 + sub_id  (sub_id ∈ [0,99])
```

详见 `Docs/design/sekiro-anim-state-machine-extraction.md`

### Step E：生成材质配置 JSON（Python）

由 `common_blender.py` 在导出 FBX 时自动生成：

**输出：`Sekiro_Materials.json`**

```json
{
  "materials": [{
    "name": "BD_M_9000",
    "blend_mode": "Opaque",
    "two_sided": false,
    "mtd": "mtd/BD_M_9000.mtd",
    "textures": {
      "_a": "BD_M_9000_a.png",
      "_n": "BD_M_9000_n.png",
      "_m": "BD_M_9000_m.png",
      "_r": "BD_M_9000_r.png"
    }
  }]
}
```

---

## 数据关系图

```
BehaviorParam_PC.json ─────┐
  variation_id             │
  behaviorJudgeID ─────────┼──→ Sekiro_TAE_Logic.json
  RefType → AtkParam_ID    │      anim_id
  Category                 │      judge_id (Type=1/2/5)
                           │      JT events (取消窗口)
AtkParam_PC.param ────────┘
  (待提取)

Output/
├── BehaviorParam_PC.json      ← ParamReader
├── Sekiro_TAE_Logic.json      ← SekiroTAEExtractor
├── Sekiro_model_hkx.json      ← FlverToJson
└── Sekiro_animations_*.json   ← SekiroAnimExtractor
```

---

## 关键数据约定

| 约定 | 说明 |
|------|------|
| 坐标系 | Y-up（保持游戏原生坐标系） |
| 单位 | 米（所有位移/顶点位置） |
| 四元数 | xyzw 顺序（与 UE FQuat 一致） |
| FLVER Euler | XZY 顺序：`Scale * RotX * RotZ * RotY * Translate` |
| UV | V 轴未翻转（原始游戏坐标系，下游需自行翻转） |

---

## 依赖关系

```
Phase 1 (提取) ────── 独立，只需游戏目录 + Yabber
Phase 2 (JSON)  ────── 依赖 Phase 1 输出 (.flver/.hkx/.mtd/.tae/.param)
Phase 3 (UE5)  ────── 依赖 Phase 2 输出的 JSON 文件
```

JSON 是中间格式，下游可为 Blender FBX 导出、UE C++ 插件导入或自定义管线，独立于 Phase 1/2 工具链。

---

## 文件清单

### Python / C# 工具链

```
Script/sekiro_asset_manager/    资产导入管线
  cli.py                         命令行入口
  pipeline_config.py             路径配置（自动探测 + settings.local.json）
  model_importer.py              模型导入编排
  animation_importer.py          动画导入编排
  material_importer.py           材质导入编排
  flver_parser.py                FLVER 解析
  mtd_parser.py                  MTD 材质定义解析
  skeleton_merge.py              骨骼合并
  ext_tools/
    SoulsFormats.dll             共享解析库（FLVER/HKX/PARAM/MTD）
    Yabber/                      万能解包器 (.dcx/.bnd/.tpf)
    FlverToJson/                 FLVER → JSON (外部)
    SekiroAnimExtractor/         HKX 动画 → JSON (外部)
    SekiroTAEExtractor/          .tae → JSON (C# 自建)
    ParamReader/                 .param → JSON (C# 自建)
    SoulsAssetPipeline/          SoulsFormats 源码
    texconv/                     DDS → PNG 转换

Tools/                           外部工具备份
  FlverToFbx/FlverToFbx/        C# 模型 JSON 生成器 (旧，已迁移到 ext_tools)
  Soul…2654 chars truncated…Yabber 1.3.1/             万能解包器 (DCX/BND4/TPF)
```

### 详细文档

| 文档 | 说明 |
|------|------|
| `Docs/sekiro-asset-import.md` | 快速参考 |
| `Docs/sekiro-asset-pipeline.md` | 本文档 |
| `Docs/pipeline-materials-unified.md` | 材质/纹理管线 |
| `Docs/sekiro-animation-system.md` | 动画系统 |
| `Docs/design/sekiro-anim-state-machine-extraction.md` | BehaviorParam ↔ AnimID 映射 |