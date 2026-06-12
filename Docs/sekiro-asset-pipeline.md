# 只狼资源导入管线

## 概述

将 FromSoftware《Sekiro: Shadows Die Twice》的角色资源（模型、骨骼、材质、动画）从游戏私有二进制格式转换为结构化 JSON 中间格式。分为两个阶段：

- **Phase 1** — 资源提取：从游戏目录解包 .dcx 压缩包，得到 .flver / .hkx / .tpf / .mtd
- **Phase 2** — JSON 生成：C# 工具将二进制格式转为结构化 JSON

---

## Phase 1：资源提取

### 工具

| 工具 | 路径 | 用途 |
|------|------|------|
| Yabber 1.3.1 | `Tools/Yabber 1.3.1/Yabber.exe` | 万能解包器：BND4/DCX/TPF |
| unpack_sekiro.py | `Script/unpack_sekiro.py` | 批量提取脚本 |
| extract_mtd.py | `Script/extract_mtd.py` | 提取 MTD 材质定义 |

### 源文件格式

| 扩展名 | 格式 | 内容 |
|--------|------|------|
| `.chrbnd.dcx` | DCX→BND4 包 | `c0000.flver`(骨骼) + `skeleton.hkx` |
| `.partsbnd.dcx` | DCX→BND4 包 | `*.flver`(模型) + `*.tpf`(纹理) |
| `.anibnd.dcx` | DCX→BND4 包 | 多个 `*.hkx` 动画文件 |
| `.mtdbnd.dcx` | DCX→BND4 包 | `*.mtd` 材质定义 |
| `.flver` | FLVER2 | 骨骼节点、网格、材质引用、纹理槽 |
| `.hkx` | Havok TAG0 | 骨骼层次+参考姿势，或动画曲线 |
| `.tpf` | Texture Pack | 多个 `.dds` 纹理 |
| `.mtd` | Material Definition | Shader路径、混合模式、纹理类型槽 |

### 提取流程

```
Sekiro Game Directory
  ├── chr/c0000.chrbnd.dcx    → Yabber → Extracted/c0000-chrbnd-dcx/
  │                              ├── c0000.flver          (骨骼 FLVER)
  │                              └── skeleton.hkx         (HKX 骨骼)
  ├── chr/c0000_*.anibnd.dcx  → Yabber → Extracted/c0000_*-anibnd-dcx/
  │                              └── a00_*.hkx            (动画文件)
  ├── parts/hd_m_9520.partsbnd.dcx → Yabber →
  │                              ├── hd_m_9520.flver      (脸部模型)
  │                              └── hd_m_9520-tpf/       (脸部纹理 .dds)
  ├── parts/bd_m_9000.partsbnd.dcx → Yabber →
  │                              ├── bd_m_9000.flver      (身体模型)
  │                              └── bd_m_9000-tpf/       (身体纹理 .dds)
  ├── parts/am_m_9000.partsbnd.dcx → Yabber → ...
  ├── parts/lg_m_9000.partsbnd.dcx → Yabber → ...
  └── Data/Data*.mtdbnd.dcx   → Yabber → Extracted/mtd/
                                 └── *.mtd                (材质定义)
```

---

## Phase 2：JSON 生成 (C# 工具)

### 工具链

| 项目 | 路径 | 输出 |
|------|------|------|
| FlverToFbx | `Tools/FlverToFbx/FlverToFbx/` | 模型 JSON |
| SekiroAnimExtractor | `Tools/SekiroAnimExtractor/` | 动画 JSON |
| SoulsAssetPipeline | `Tools/SoulsAssetPipeline/` | 共享库（FLVER/HKX/MTD 解析） |

### Step A：生成模型 JSON

```bash
FlverToFbx.exe \
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

### Step C：生成材质配置 JSON（Blender Python）

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
Phase 2 (JSON)  ────── 依赖 Phase 1 的输出 (.flver/.hkx/.mtd)
```

JSON 是中间格式，下游可为 Blender FBX 导出、UE C++ 插件导入或自定义管线，独立于 Phase 1/2 工具链。

---

## 文件清单

### Python / C# 工具链

```
Script/                           Python 脚本（Blender/UE5/诊断）
  common_blender.py               Blender 共享库 (骨骼/材质/网格/动画/FBX)
  export_common_anims.py          完整导出脚本 (模型+动画+材质配置)
  import_model.py                 仅模型 FBX 导出
  import_anims.py                 仅动画 FBX 导出
  unpack_sekiro.py                资源提取
  extract_mtd.py                  MTD 材质提取
  ue5_import_character.py         UE5 Python 导入 (FBX→UAssets)
  ue5_setup_materials.py          UE5 Python 材质配置
  check_*.py / diag_*.py         诊断/审计脚本

Tools/FlverToFbx/FlverToFbx/      C# 工具（模型/动画 JSON 生成器）
  Program.cs                      C# 模型 JSON 生成器

Tools/SoulsAssetPipeline/
  SoulsFormats/Formats/FLVER/    FLVER2 解析器
  SoulsFormats/Formats/MTD.cs    MTD 材质定义解析
  SoulsAssetPipeline/Animation/  HKX 骨骼/动画解析器
  Havoc/                         Havok TAG0 二进制读写

Tools/Yabber 1.3.1/             万能解包器 (DCX/BND4/TPF)
```
