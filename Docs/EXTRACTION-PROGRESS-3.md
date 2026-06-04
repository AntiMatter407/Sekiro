# Sekiro 角色提取进度记录（第三次会话）

日期：2026-06-03

---

## 当前状态总结

### 已完成（含本次）
- v10 材质修复：35/35材质获得纹理（从v9的8/35大幅提升）
- 手部动画抽搐根因诊断完成，修复方案已确认（待实施）
- 动画专用骨骼影响分析完成（武器挂点等不受影响）

---

## 问题6：材质空白（v9 → v10 已修复）

### 症状
导入 v9 FBX 后部分材质显示为白色/空白。

### 三层根因

1. **`used_textures` 独占约束错误**：`assign_textures_globally` 限制每张纹理只能分配给一个材质，但游戏中多材质共享同一 tiling 纹理是标准做法。

2. **命名不匹配**：
   - HD_M_9510：纹理叫 "dammy"（日式英语，dummy之意），MTD 是 "Decal" → 得分 0
   - AM_M_9000：纹理叫 "Armor"，材质名是 "tops"/"body" → 得分 0
   - "tiling" 前缀无法匹配 "tilingset1"/"tilingchain" 等变体

3. **源纹理提取不全**：仅提取了 26 张纹理（4个part: HD/BD/AM/LG），缺少部分材质的专用纹理（tilingchain、tilingrope、muffler等），但同一 part 内存在可用的 fallback 纹理。

### 修复内容（`export_common_anims.py`）

**`score_texture_candidate`**：
- 新增子串匹配：token 前缀重叠（如 "tiling" ↔ "tilingset1"/"tilingchain"）得 10 分
- 降低最小 token 长度从 3→2 以捕获更多匹配对

**`assign_textures_globally`**：
- 去掉 `used_textures` 独占约束，仅保留 `filled_slots`（每个材质每个 suffix 只填一次）
- 新增第二遍**回退分配**：无纹理的材质自动获取同 Part 的可用纹理（优先 `_a` albedo）

**`create_materials`**：
- 新增 `_r`（roughness）贴图支持

**`strip_texture_suffix`**：
- 新增 `_r` suffix 识别

### 效果对比

| 指标 | v9 | v10 |
|------|-----|------|
| 有纹理的材质 | 8/35 | **35/35** |
| 有 albedo 的材质 | 8/35 | **35/35** |
| 有 normal 的材质 | 5/35 | **17/35** |

> **注意**：部分材质通过 fallback 分配了同 Part 的共享纹理（如 tilingchain 使用 tilingset1 纹理），视觉效果可能不完全匹配原游戏，但远好于全白。后续可补充提取缺失纹理进行精细化分配。

---

## 问题7：手部动画抽搐（已诊断，待修复）

### 症状
角色手部动画严重抽搐（twitching），其他骨骼动画正常。

### 根因：模型与动画参考姿势不匹配

**C# 提取器差异**：
- **FLVER 模型提取器**（`FlverToFbx/Program.cs`）：使用 Euler X→Z→Y 顺序 + `localMat * parentWorld` 矩阵组合
- **HKX 动画提取器**（`SekiroAnimExtractor/Program.cs`）：直接读取 Havok 四元数 local transforms

**结果**：两个提取器计算出的同名骨骼世界位置存在系统性差异：

| 骨骼 | 模型 X | 动画 X | 偏差 | 骨骼长度 | 偏差/长度比 |
|------|--------|--------|------|----------|------------|
| Spine | 0.00 | 0.00 | 0.00 | ~0.09 | 0% |
| Spine1 | 0.00 | 0.00 | 0.12 | ~0.09 | 133% |
| L_UpperArm | 0.17 | 0.48 | 0.44 | ~0.30 | 147% |
| L_Hand | 0.53 | 0.99 | 0.46 | ~0.20 | 230% |
| L_Finger02 | 0.58 | 1.10 | 0.53 | ~0.04 | **1325%** |
| R_Finger22 | -0.66 | 1.17 | 1.84 | ~0.03 | **6133%** |

- 80/86 共享骨骼差异 > 0.1 单位
- 差异沿骨骼链累积增大（根部匹配 → 末端偏差巨大）
- 手指骨骼仅 0.03-0.05 单位长，却偏离加权顶点 0.5-1.8 单位
- 骨骼旋转时产生巨大**杠杆效应** → 手指抽搐

### 修复方案：动画重定向（Retargeting）

**核心公式**：
```
delta = A_ref_local⁻¹ @ A_frame_local        （提取动画变化量）
M_frame_local = M_ref_local @ delta           （应用到模型参考姿势）
M_frame_world[i] = M_frame_world[parent] @ M_frame_local  （累积世界矩阵）
```

**`build_armature` 修改**：
- 共享骨骼（模型和动画都有，86个）：使用模型 `WorldPos`/`WorldRot` 放置 edit_bone
- 动画专用骨骼（60个面部/武器/IK等）：保持动画参考姿势
- 父级关系不变

**`add_actions` 修改**：
- 改为 delta-based 计算
- 去掉 `bpy.context.view_layer.update()`（避免 Blender 二次分解引入误差）

### 动画专用骨骼影响分析

**结论：不受负面影响，反而更准确。**

- 动画专用骨骼通过 `parent @ local` 跟随父骨骼，相对偏移不变
- 武器挂点（L_Weapon、B_Wepon_Case、Sheath 等）吸附在共享骨骼上，重定向后位置匹配模型身体
- 面部骨骼（60个）整体跟随 Head 偏移，相对表情动画不受影响
- UE5 Socket 挂载依赖局部偏移，与重定向完全兼容

---

## 当前脚本文件

| 文件 | 用途 | 状态 |
|------|------|------|
| `Tools/FlverToFbx/export_common_anims.py` | 主构建脚本 | **v10（材质已修复）** |
| `Tools/FlverToFbx/extract_common_anims.py` | 从全量 JSON 提取4个通用动画 | 不变 |
| `Tools/FlverToFbx/convert_dds_to_png.py` | Blender DDS转PNG（已被Pillow+texconv替代） | 不变 |
| `Tools/FlverToFbx/unpack_sekiro.py` | Yabber 批量解包 | 不变 |
| `Tools/SekiroAnimExtractor/Program.cs` | HKX动画提取器（C#） | 不变 |
| `Tools/FlverToFbx/FlverToFbx/Program.cs` | FLVER模型提取器（C#） | 不变 |

## FBX 版本历史

| 版本 | 文件 | 主要变化 | 状态 |
|------|------|---------|------|
| v1~v4 | （早期版本） | 骨骼坍塌、单 body、无动画 | 废弃 |
| v5 | `Sekiro_CommonAnims_UnionSkeleton_v5.fbx` | 152并集骨骼，但材质全白，无动画 | 废弃 |
| v6 | `Sekiro_CommonAnims_UnionSkeleton_v6.fbx` | PNG贴图+NLA导出，8个动画（重复） | 废弃 |
| v7 | `Sekiro_CommonAnims_UnionSkeleton_v7.fbx` | 同v6，确认PNG缓存正常 | 废弃 |
| v8 | `Sekiro_CommonAnims_UnionSkeleton_v8.fbx` | 世界矩阵动画，4个动画，但骨骼扭曲 | 废弃 |
| v9 | `Sekiro_CommonAnims_UnionSkeleton_v9.fbx` | 骨骼 roll 对齐 + 材质全局分配 | 废弃（材质不全+手抽搐） |
| **v10** | `Sekiro_CommonAnims_UnionSkeleton_v10.fbx` | **材质共享+fallback匹配（35/35）** | **待生成验证** |
| v11 | （计划中） | **动画重定向修复（模型参考姿势+delta）** | 待实施 |

---

## 待完成

1. 运行 `export_common_anims.py` 生成 v10 FBX，验证材质
2. 实施动画重定向修复（v11）
3. 验证 v11：动画无抽搐、武器挂点正确、4个AnimSequence
