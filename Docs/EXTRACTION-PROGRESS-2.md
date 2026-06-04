# Sekiro 角色提取进度记录（第二次会话）

日期：2026-06-02

---

## 当前状态总结

### 已完成
- 模型 JSON：`Sekiro_model.json`（92骨骼、35材质、46网格、102743顶点）
- 动画 JSON：`Sekiro_animations.json`（1434个真实动画，~1GB）
- 通用动画子集：`Sekiro_common_anims.json`（4个动画：Idle/Walk/Run/Sprint）
- FBX 版本历史：v1~v9，见下方
- DDS → PNG 转换：26张全部完成（5张 BC1_SRGB 用 texconv 转，其余用 Pillow 转）

---

## 本次会话处理的问题及修复

### 问题1：材质全白（v5）
**根因**：`Sekiro_model.json` 里所有 `Materials[].Textures` 都是空对象 `{}`，C# 提取阶段未写入贴图路径。

**修复**：在 `export_common_anims.py` 中改用启发式匹配——按 `Part` 前缀 + 材质名/MTD 名中的语义词对 DDS/PNG cache 打分，再全局分配（每张贴图只给得分最高的材质）。

### 问题2：UE5 导入 DDS 报错（v6/v7 过渡期）
```
Error: DDS DXGIFormat not supported : 72 : BC1_UNORM_SRGB
Error: DDS DXGIFormat not supported : 98 : BC7_UNORM
```
**根因**：UE5 FBX 导入不支持 BC1_SRGB 和 BC7 格式 DDS。

**修复**：
- 安装 Pillow：`pip install Pillow` → 转换了 21 张
- 下载 `texconv.exe`（Microsoft DirectXTex）→ 转换了剩余 5 张 BC1_SRGB
- 脚本改为 PNG 优先（同 stem 时 PNG 覆盖 DDS）

### 问题3：导入出现 8 个动画序列（v6）
**根因**：同时开启 `bake_anim_use_all_actions=True` + `bake_anim_use_nla_strips=True`，每个动作被导出两次。

**修复**：改为 `bake_anim_use_all_actions=False`，只走 NLA strips；每个 Action 只创建一个 NLA strip，`arm_obj.animation_data.action = None`。

### 问题4：动画骨骼完全扭曲（v8）
**根因**：`build_armature` 里 edit_bone 只设了 `head` 和 `tail`，没有设 `roll`，导致 Blender bone 的 rest matrix ≠ HKX 参考姿势矩阵。`pb.matrix =` 赋值时 Blender 内部做 `rest_mat_inv @ world_mat`，rest 矩阵错误 → 扭曲。

**修复**（v9）：
```python
bone_y = wrot @ mathutils.Vector((0, 1, 0))
bone_z = wrot @ mathutils.Vector((0, 0, 1))
eb.tail = head + bone_y * 0.05
eb.align_roll(bone_z)  # 使 rest matrix == HKX 参考姿势矩阵
```

### 问题5：材质错乱（v8/v9之前）
**根因**：旧打分函数对同 Part 前缀的所有 DDS 都加 +50，tiling 等关键词又额外加分，导致同一张贴图得分在多个材质上接近，被重复分配。

**修复**（v9）：
- 重写 `score_texture_candidate`：先做 Part 前缀过滤（不同 part 直接返回 0），再剥掉 part 前缀后比较语义核心
- 新增 `assign_textures_globally`：全局贪婪分配，每张贴图每个 suffix 只给得分最高的一个材质
- 结果：`albedo 8/35`（从 35→8），说明 tiling 等共享贴图材质不再重复分配

---

## FBX 版本历史

| 版本 | 文件 | 主要变化 | 状态 |
|------|------|---------|------|
| v1~v4 | （早期版本） | 骨骼坍塌、单 body、无动画 | 废弃 |
| v5 | `Sekiro_CommonAnims_UnionSkeleton_v5.fbx` | 152并集骨骼，但材质全白，无动画 | 废弃 |
| v6 | `Sekiro_CommonAnims_UnionSkeleton_v6.fbx` | PNG贴图+NLA导出，8个动画（重复） | 废弃 |
| v7 | `Sekiro_CommonAnims_UnionSkeleton_v7.fbx` | 同v6，确认PNG缓存正常 | 废弃 |
| v8 | `Sekiro_CommonAnims_UnionSkeleton_v8.fbx` | 世界矩阵动画，4个动画，但骨骼扭曲 | 废弃 |
| **v9** | `Sekiro_CommonAnims_UnionSkeleton_v9.fbx` | **骨骼 roll 对齐 + 材质全局分配** | **当前最新** |

---

## 当前脚本文件

| 文件 | 用途 |
|------|------|
| `Tools/FlverToFbx/export_common_anims.py` | 主构建脚本（v9 状态） |
| `Tools/FlverToFbx/extract_common_anims.py` | 从全量 JSON 提取4个通用动画 |
| `Tools/FlverToFbx/convert_dds_to_png.py` | Blender DDS转PNG（已被Pillow+texconv替代） |
| `Tools/FlverToFbx/unpack_sekiro.py` | Yabber 批量解包 |
| `Tools/SekiroAnimExtractor/Program.cs` | HKX动画提取器（C#） |
| `Tools/FlverToFbx/FlverToFbx/Program.cs` | FLVER模型提取器（C#） |

---

## 待验证（v9 导入 UE 后观察）

1. 动画是否正常（骨骼旋转不扭曲）
2. ~~材质贴图是否正确分配到各部位~~ → v10修复，见下方
3. 模型朝向是否正确（当前有 +90°X 旋转补偿）
4. 是否恰好 4 个 AnimSequence

---

## v10 修复：材质匹配改进（2026-06-03）

### 问题6：35个材质中仅8个有纹理（v9状态）

**根因**：
1. `assign_textures_globally` 的 `used_textures` 集合阻止纹理共享 —— 游戏中多材质共享同一tiling纹理
2. 命名不匹配：HD_M_9510纹理叫"dammy"但MTD是"Decal"→得分0；AM_M_9000纹理叫"Armor"但材质名是"tops"/"body"→得分0
3. "tiling"前缀无法匹配"tilingset1"/"tilingchain"等变体

**修复**（`export_common_anims.py`）：
- 去掉 `used_textures` 独占约束 → 允许多材质共享纹理
- 加入子串匹配：token前缀重叠（如"tiling"↔"tilingset1"）得10分
- 加入第二遍回退分配：无纹理的材质自动分配同Part的任意可用纹理（优先_a）

**结果**：35/35材质获得纹理（从8→35），其中17个获得法线贴图

### 待解决：手部动画抽搐

**诊断**：模型(FLVER)和动画(HKX)的参考姿势骨骼世界位置差异大。
- 根骨骼匹配（Spine/Pelvis dist≈0）
- 差异沿骨骼链累积增大（L_Hand: 0.46, R_Finger22: 1.84）
- 手指骨骼仅0.03-0.05单位长，却偏离骨骼位置0.5-1.8单位 → 旋转时杠杆效应→抽搐
- 原因：FLVER提取使用Euler X→Z→Y顺序 + `local @ parent` 矩阵组合，HKX使用四元数 + `parent @ local`

**修复方向**：`build_armature` 使用model WorldPos构建edit_bone（共享骨骼），保持动画local transform不变。骨骼层级一致故可直接应用。

---

## 关键技术参数

- 骨骼：模型92 / 动画146 / 并集152
- HKX 骨骼顺序：父索引 < 子索引（标准顺序，支持顺序遍历累积世界矩阵）
- Blender FBX 导出设置：`axis_forward='-Z'`, `axis_up='Y'`, `primary_bone_axis='Y'`
- 动画数据：局部变换（local transform），需从根到叶累积成世界矩阵后赋给 `pb.matrix`
- texconv 路径：`D:\Sekiro\Tools\texconv.exe`
- PNG 贴图路径：各 partsbnd 解包目录下的 `-tpf` 子目录内
