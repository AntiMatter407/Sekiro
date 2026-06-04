# Sekiro FLVER → FBX 对话状态

## 时间
2026-06-04

## 最新修复（2026-06-04 下午）

### 问题 1：纹理分配错误（胡须拿到 head 纹理、fur 拿到 beard 纹理）★ 本次修复
- **根因 1**：Decal 覆写逻辑对所有 Decal 材质（含 FC_M_0100 自身）清空纹理并重新分配，FC_M 材质被错误覆写
- **根因 2**：同义词表 `'beard': ['beard', 'fur', 'decal']` 中 `'fur'` 被列为 beard 同义词，导致 beard 纹理对 fur 材质得分最高
- **根因 3**：同义词表仅在 `score==0` 时触发，而 FC 通用前缀（'fc', '0100'）已让所有 FC 纹理得 40 分，同义词永远不触发，正确类型匹配无法打破平局
- **修复**：
  1. Decal 覆写跳过 FC_M 自身材质（`not part.startswith('fc_')`）
  2. 同义词表移除 `'beard': ['fur']`（beard 不应匹配 fur）
  3. 同义词匹配改为始终生效（FC 材质），追加 +60 分以打破 token 平局
- **验证结果**：
  - Beard Decal → beard01_a/n ✓
  - Eye → eye_a/n ✓
  - Head_AO_SSS → head_a/n ✓
  - Fur/SSS → hair2_a/n ✓
  - Hair Decal → hair_a/n ✓

### 问题 2：低模感（顶点法线未应用）
- Blender 5.1.2 的 `normals_split_custom_set_from_vertices` 在 `from_pydata` 后调用会崩溃
- **修复**：用 BMesh API 替代 `from_pydata`，然后调用 `normals_split_custom_set_from_vertices`
- 已验证自定义法线在 FBX 导出/导入后保留（round-trip 测试通过）

### 问题 3：法线反转（已解决）
- `export_common_anims.py/build_meshes` L490 三角形绕序为 `(t[0], t[1], t[2])`
- `common_blender.py/_create_mesh_obj` L716 绕序为 `(t[0], t[2], t[1])`（正确）
- 两个 build_meshes 独立实现，未同步，导致 Anim FBX 法线反转
- **修复**：统一为 `(t[0], t[2], t[1])`

### 问题 3：HD_L/R_bone 天线（已解决）
- HD_M_9520 Decal 网格蒙皮到 HD_L/R_bone，rest pose 中骨骼笔直朝上
- 该网格仅 HD_M_9520 使用，无其他依赖
- **修复**：从 body FLVER 列表移除 HD_M_9520.flver

### 问题 4：角色朝向（已解决）
- FLVER 右手坐标系(Y-up,Z-forward)无法通过纯旋转同时满足 Y-up + -Z-forward
- **修复**：Empty parent Z=180° + Armature child X=90° + `axis_forward='-Z'`

### 问题 5：法线贴图（BC5 DirectX 格式）
- DirectX BC5 法线贴图仅 R+G 通道编码法线，G 通道与 OpenGL 方向相反
- **修复**：Pillow 直接修改纹理文件 G 通道取反（FBX 不保留 shader nodes）

## 当前进度

### 已解决的问题
1. **FC_M_0100 面部/头发模型遗漏** — 原始导出只包含 BD/AM/HD/LG 四个 Part，面部+头发的 FC_M_0100 FLVER 完全未被包含
2. **HD_L/R_bone 不是头发骨骼** — HD_M_9520 的 HD_L/R_bone 是头部 Decal（贴花）骨骼，不是头发。Sekiro 的头发是扎起来的发髻，无需物理下垂
3. **HKX 四元数统一** — Program.cs 读取 skeleton.hkx 的四元数计算 WorldPos/WorldRot
4. **骨骼查找合并** — combinedParentIdx 从 skeleton FLVER + 所有 body FLVER 合并

### 本次修复（2026-06-04）

**问题**：用户反馈"HD_L/R_bone 对应的不是我想要的头发，角色头发是扎起来的"。

**根因分析**：
- 原始导出命令遗漏了 `FC_M_0100.flver`（位于 `Extracted/fc_m_0100-partsbnd/parts/Face/FC_M_0100/FC_M_0100.flver`）
- FC_M_0100 是真正的面部+头发模型：12 个网格、33,549 个顶点、250 个骨骼
- 材质包含：fur（毛发）、fur_SSS、Head_AO_SSS、beard_Decal、EyeAnime、Eye_Crystal
- 所有 FC_M_0100 网格顶点 100% 蒙皮到 Head 骨骼（部分到 Neck/Spine2）
- FC_M_0100 的头发骨骼（FC_M_0100_hair 等）在 palette 中但无顶点引用——扎起来的发髻完全跟随 Head 运动
- HD_L/R_bone（HD_M_9520）是头部 Decal 贴花骨骼，不是头发

**修复**：
1. 重新运行 Program.cs，将 FC_M_0100.flver 加入 body FLVER 列表
2. 更新 `_PHYSICS_BONE_PATTERNS`：移除 `HD_L_bone`、`HD_R_bone`，只保留 Skirt/bonecloth
3. 更新 `_simulate_hair_gravity` 文档字符串：明确这是 cloth/skirt 物理，不是头发
4. 重新生成 Model FBX 和 Anim FBX

### 历史修复（已废弃/演进）

**Verlet 链式物理模拟（替代 _fit_hair_bones_to_vertices）**：
`_simulate_hair_gravity()` 使用 Verlet 积分（300 迭代，重力 -Y）对 Skirt/bonecloth 骨骼链进行物理下垂模拟：
- POSE 模式旋转骨骼指向 Verlet 目标
- Bake armature modifier → 网格顶点变形
- Bake pose as rest pose → 重新添加 armature modifier

**旧方案（已废弃）**：
- `_fit_hair_bones_to_vertices`：从顶点权重质心驱动骨骼，被 Verlet 方案替代
- `_apply_hair_gravity`：固定角度公式旋转，被 _fit_hair_bones_to_vertices 替代

## 修改的文件

### D:\Sekiro\Tools\FlverToFbx\common_blender.py

```python
# L38-99: score_texture_candidate — 修复同义词表（移除 beard→fur）+
#   同义词匹配始终生效（+60分打破FC通用token平局）

# L158-190: assign_textures_globally — Decal覆写跳过FC_M自身材质

# L438: _PHYSICS_BONE_PATTERNS — 移除 HD_L/R_bone

# L722-752: _build_mesh_from_verts() — BMesh构建 + FLVER顶点法线
#   替代 from_pydata，避免 Blender 5.1.2 崩溃

# L895-906: apply_scene_orientation_fix — Empty Z=180° + Armature X=90°
```

### D:\Sekiro\Tools\FlverToFbx\export_common_anims.py

```python
# L15: from common_blender import _build_mesh_from_verts
# L487: build_meshes 使用 _build_mesh_from_verts 替代 from_pydata
```

### D:\Sekiro\Tools\FlverToFbx\FlverToFbx\Program.cs
- 支持 --skeleton-hkx 参数加载 HKX skeleton
- ComputeBoneWorld 优先使用 HKX 四元数
- 合并 skeleton + body FLVER 的骨骼查找表

### D:\Sekiro\Tools\FlverToFbx\FlverToFbx\FlverToFbx.csproj
- net9.0-windows, AllowUnsafeBlocks=true
- 引用 Havoc/SoulsFormats/SoulsAssetPipeline

## 关键数据

### 模型结构
| Part | FLVER | 网格数 | 材质数 | 说明 |
|------|-------|--------|--------|------|
| BD_M_9000 | Body | 27 | 16 | 身体 |
| AM_M_9000 | Arm | 11 | 11 | 义肢/手臂 |
| HD_M_9520 | Head Decal | 1 | 1 | 头部贴花（Decal） |
| LG_M_9000 | Leg | 7 | 7 | 腿部 |
| **FC_M_0100** | **Face/Hair** | **12** | **12** | **面部+头发（新增）** |

### 骨骼统计
- 输出骨骼：93（89 被顶点引用 + 4 祖先）
- 合并查找表：518（skeleton 467 + body FLVER 补充）
- HKX 动画骨骼：146
- 头发相关骨骼（FC_M_0100 中，但无顶点引用）：FC_M_0100_hair、FC_M_0100_hair01_cloth、FC_M_0100_hair02_cloth、FC_M_0100_HDhair

### 纹理
- 面部：FC_M_0100_head_a/n/m/1m/AO.png (2048x2048)
- 头发：FC_M_0100_hair_m/n.png, hair2_m/n/r.dds
- 胡须：FC_M_0100_beard01_a/n.png
- 眼睛：FC_M_0100_eye_a/d/n/1m.png
- 动画：Sekiro_Idle(101帧), Walk(81帧), Run(81帧), Sprint(51帧)

## 输出文件

| 文件 | 路径 | 大小 |
|------|------|------|
| Model JSON | D:\Sekiro\Extracted\Sekiro_model_hkx.json | 120MB |
| Anim JSON | D:\Sekiro\Extracted\Sekiro_common_anims.json | - |
| Model FBX | D:\Sekiro\Extracted\Sekiro_Model.fbx | 23MB |
| Anim Model FBX | D:\Sekiro\Extracted\Sekiro_Anim_Model.fbx | 24MB |
| Anim FBX | D:\Sekiro\Extracted\Sekiro_Anim_Anim.fbx | 3.3MB |
| Textures | D:\Sekiro\Extracted\Textures\ | - |

## Blender 版本
Blender 5.1.2 (C:/Program Files/Blender Foundation/Blender 5.1/blender.exe)

## 待验证
- [x] 角色朝向（Empty Z=180° + Armature X=90° + axis_forward='-Z'）
- [x] 法线贴图格式（BC5，G通道反转）
- [x] 顶点法线应用（BMesh + normals_split_custom_set_from_vertices → FBX round-trip 验证通过）
- [x] 纹理分配正确（beard→beard01, fur→hair2, head→head, eye→eye, hair→hair）
- [ ] UE5 导入后低模感是否消除（纹理修正 + 顶点法线修正后）
- [ ] FC_M_0100 面部/头发在 UE5 中是否正常显示
- [ ] Skirt/bonecloth 物理下垂是否自然

## 运行命令
```bash
# 重新生成 Model JSON（5 个 body FLVER，含 FC_M_0100）
dotnet run -c Release --project D:/Sekiro/Tools/FlverToFbx/FlverToFbx -- \
  "Extracted/c0000-chrbnd-dcx/chr/c0000/c0000.flver" \
  "Extracted/bd_m_9000-partsbnd-dcx/parts/FullBody/BD_M_9000/BD_M_9000.flver" \
  "Extracted/am_m_9000-partsbnd-dcx/parts/FullBody/AM_M_9000/AM_M_9000.flver" \
  "Extracted/hd_m_9520-partsbnd-dcx/parts/FullBody/HD_M_9520/HD_M_9520.flver" \
  "Extracted/lg_m_9000-partsbnd-dcx/parts/FullBody/LG_M_9000/LG_M_9000.flver" \
  "Extracted/fc_m_0100-partsbnd/parts/Face/FC_M_0100/FC_M_0100.flver" \
  --skeleton-hkx "Extracted/c0000-anibnd-dcx/chr/c0000/hkx/skeleton.hkx" \
  -o "Extracted/Sekiro_model_hkx.json"

# 重新生成 Model FBX
cd D:/Sekiro/Tools/FlverToFbx && "C:/Program Files/Blender Foundation/Blender 5.1/blender.exe" --background --python import_model.py -- D:/Sekiro/Extracted/Sekiro_model_hkx.json D:/Sekiro/Extracted/Sekiro_common_anims.json D:/Sekiro/Extracted/Sekiro_Model.fbx D:/Sekiro/Extracted/

# 重新生成 Anim FBX
cd D:/Sekiro/Tools/FlverToFbx && "C:/Program Files/Blender Foundation/Blender 5.1/blender.exe" --background --python export_common_anims.py -- D:/Sekiro/Extracted/Sekiro_model_hkx.json D:/Sekiro/Extracted/Sekiro_common_anims.json D:/Sekiro/Extracted/Sekiro_Anim.fbx D:/Sekiro/Extracted/
```
