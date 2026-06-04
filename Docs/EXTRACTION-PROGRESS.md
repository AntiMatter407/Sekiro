# Sekiro 角色提取工作记录

> 日期: 2026-06-01
> 状态: 进行中 (阶段2B 动画提取 - 等待 NuGet 包下载)

---

## 一、已完成的工作

### 1. 骨骼旋转修复 (FlverToFbx/Program.cs)

**问题**: FLVER 模型导入 Blender 后骨骼坍塌/错乱。

**根因**: `Program.cs` 使用 `Matrix4x4.CreateFromYawPitchRoll()` 构建旋转矩阵，其内部顺序为 Z→X→Y，但 FLVER 格式的正确欧拉旋转顺序是 **X→Z→Y**（参考 DSAnimStudio `NewBone.cs:215-218`）。

**修复**:
```csharp
// 修复前 (错误):
localMat *= Matrix4x4.CreateFromYawPitchRoll(localRot.Y, localRot.X, localRot.Z);

// 修复后 (正确 X→Z→Y):
localMat *= Matrix4x4.CreateRotationX(localRot.X);
localMat *= Matrix4x4.CreateRotationZ(localRot.Z);
localMat *= Matrix4x4.CreateRotationY(localRot.Y);
```

同时改进了世界矩阵缓存：从分解值 `(pos, rot, scale)` 改为同时缓存完整矩阵 `(pos, rot, scale, mat)`，子骨骼直接用完整矩阵相乘，避免分解→重组的精度累积误差。

**验证**: BD_M_9000 身体部件骨骼位置已修正（Spine2 X坐标从 0.176 修正到 ~0，符合身体中线）。

### 2. 阶段1：资产批量解包

**脚本**: `Tools/FlverToFbx/unpack_sekiro.py`

**游戏路径**: `D:\SteamLibrary\steamapps\common\Sekiro`
**输出路径**: `D:\Sekiro\Extracted\`

**解包内容**:
- 角色骨骼: `c0000.chrbnd.dcx` → `c0000.flver` + `c0000.HKX`
- 头部: `hd_m_9510.partsbnd.dcx` → `HD_M_9510.flver` + TPF纹理
- 身体: `bd_m_9000.partsbnd.dcx` → `BD_M_9000.flver` + TPF纹理 (之前已解包)
- 手臂: `am_m_9000.partsbnd.dcx` → `AM_M_9000.flver` + TPF纹理
- 腿部: `lg_m_9000.partsbnd.dcx` → `LG_M_9000.flver` + TPF纹理
- 动画: 44 个 anibnd.dcx 文件 → 1438 个 .hkx 动画文件
- 纹理: 4 个 TPF 文件 → 26 个 DDS 纹理

**Yabber 路径**: `D:\Sekiro\Tools\Yabber 1.3.1\Yabber.exe`

### 3. 阶段2A：多部件模型合并 + 材质导出

**修改文件**: `Tools/FlverToFbx/FlverToFbx/Program.cs`

Program.cs 已从单 FLVER 提取器扩展为多部件合并工具：
- 支持命令行传入多个 body FLVER 文件
- 合并所有部件的网格、材质
- 导出材质名称、MTD 引用、纹理采样器路径
- 使用 `-o` 参数指定输出路径

**运行命令**:
```powershell
dotnet run --project D:\Sekiro\Tools\FlverToFbx\FlverToFbx\FlverToFbx.csproj -c Release -- ^
  D:\Sekiro\Extracted\c0000-chrbnd-dcx\chr\c0000\c0000.flver ^
  D:\Sekiro\Extracted\hd_m_9510-partsbnd-dcx\parts\FullBody\HD_M_9510\HD_M_9510.flver ^
  D:\Sekiro\Extracted\bd_m_9000-partsbnd-dcx\parts\FullBody\BD_M_9000\BD_M_9000.flver ^
  D:\Sekiro\Extracted\am_m_9000-partsbnd-dcx\parts\FullBody\AM_M_9000\AM_M_9000.flver ^
  D:\Sekiro\Extracted\lg_m_9000-partsbnd-dcx\parts\FullBody\LG_M_9000\LG_M_9000.flver ^
  -o D:\Sekiro\Extracted\Sekiro_model.json
```

**输出结果**: `D:\Sekiro\Extracted\Sekiro_model.json`
- 92 骨骼, 35 材质, 46 网格
- 102,743 顶点, 560,314 三角面

### 4. 阶段2B：动画提取器 (进行中)

**新项目**: `D:\Sekiro\Tools\SekiroAnimExtractor\`
- `SekiroAnimExtractor.csproj` - 引用 SoulsAssetPipeline + SoulsFormats + Havoc
- `Program.cs` - 动画提取主程序

**依赖库**: `D:\Sekiro\Tools\SoulsAssetPipeline\` (已 git clone)
- 源码: https://github.com/Meowmaritus/SoulsAssetPipeline

**当前阻塞**: NuGet 包下载超时。需要手动下载以下 7 个包：
1. BouncyCastle.Cryptography 2.4.0
2. ZstdNet 1.4.5
3. DrSwizzler 1.1.1
4. System.Text.Encoding.CodePages 8.0.0
5. System.ValueTuple 4.5.0
6. AssimpNet 4.1.0
7. TeximpNet 1.4.3

**恢复构建命令**:
```powershell
# 如果手动下载了 .nupkg 到 D:\Sekiro\Tools\nuget-local\:
dotnet restore D:\Sekiro\Tools\SekiroAnimExtractor\SekiroAnimExtractor.csproj --source D:\Sekiro\Tools\nuget-local --source https://api.nuget.org/v3/index.json

# 或者网络恢复后直接:
dotnet restore D:\Sekiro\Tools\SekiroAnimExtractor\SekiroAnimExtractor.csproj

# 然后构建:
dotnet build D:\Sekiro\Tools\SekiroAnimExtractor\SekiroAnimExtractor.csproj -c Release
```

**运行命令** (构建成功后):
```powershell
dotnet run --project D:\Sekiro\Tools\SekiroAnimExtractor\SekiroAnimExtractor.csproj -c Release -- ^
  D:\Sekiro\Extracted\c0000-anibnd-dcx\chr\c0000\hkx\skeleton.hkx ^
  D:\Sekiro\Extracted ^
  D:\Sekiro\Extracted\Sekiro_animations.json ^
  --sample-rate 30
```

---

## 二、待完成的工作

### 阶段2B 续：动画提取
1. 解决 NuGet 包下载问题
2. 构建 SekiroAnimExtractor
3. 运行提取所有 1438 个 HKX 动画
4. 验证动画数据正确性

### 阶段3：Blender 脚本合并导出 FBX
需要创建 `Tools/FlverToFbx/sekiro_build.py`：
1. 从 `Sekiro_model.json` 构建骨架 + 网格
2. 从 DDS 纹理应用材质
3. 从 `Sekiro_animations.json` 导入所有动画为 Blender Actions
4. 导出单个 `Sekiro.fbx`（包含模型+骨骼+所有动画）

---

## 三、关键技术发现

### FLVER 旋转顺序
FLVER 格式的欧拉旋转顺序是 **X→Z→Y**（不是 YPR/ZXY）。
参考权威实现: DSAnimStudio `NewBone.cs:215-218`

### Sekiro HKX 格式
- Sekiro 使用 Havok 2015 TAG0 格式
- 读取方式: `HKX.GenFakeFromTagFile(hkxBytes, compendiumBytes)`
- 无需降级转换，直接读取
- 每个动画目录下有 `.compendium` 文件需要一起加载
- 动画主要是 SplineCompressed 格式

### SoulsAssetPipeline 动画 API
```csharp
// 关键调用链:
HKX hkx = HKX.GenFakeFromTagFile(bytes, compendium);
// 从 hkx.DataSection.Objects 提取:
//   HKX.HKASplineCompressedAnimation / HKX.HKAInterleavedUncompressedAnimation
//   HKX.HKAAnimationBinding
//   HKX.HKADefaultAnimatedReferenceFrame
//   HKX.HKASkeleton

// 创建动画数据包装:
var animData = new HavokAnimationData_SplineCompressed(id, name, skeleton, refFrame, binding, anim);

// 获取每帧每骨骼变换:
NewBlendableTransform t = animData.GetTransformOnFrameByBone(boneIndex, frame, looping);
// t.Translation (Vector3), t.Rotation (Quaternion), t.Scale (Vector3)
```

---

## 四、文件清单

### 修改过的文件
| 文件 | 状态 | 说明 |
|-----|------|------|
| `Tools/FlverToFbx/FlverToFbx/Program.cs` | 已修改 | 旋转修复 + 多部件合并 + 材质导出 |

### 新创建的文件
| 文件 | 状态 | 说明 |
|-----|------|------|
| `Tools/FlverToFbx/unpack_sekiro.py` | 完成 | 批量解包脚本 |
| `Tools/SekiroAnimExtractor/SekiroAnimExtractor.csproj` | 完成 | 动画提取器项目配置 |
| `Tools/SekiroAnimExtractor/Program.cs` | 完成 | 动画提取器主程序 |

### 已生成的资产
| 文件 | 说明 |
|-----|------|
| `Extracted/Sekiro_model.json` | 合并后的完整模型 JSON (92骨骼+35材质+46网格) |
| `Extracted/BD_M_9000_blender_v4.json` | 旋转修复后的 v4 身体模型 JSON |
| `Extracted/BD_M_9000_v4.fbx` | 旋转修复后的 v4 身体模型 FBX |
| `Extracted/*-partsbnd-dcx/` | 解包的部件目录 (头/身/臂/腿) |
| `Extracted/c0000*-anibnd-dcx/` | 解包的 44 个动画包目录 |
| `Extracted/**/*.dds` | 26 个 DDS 纹理文件 |

### 外部依赖
| 路径 | 说明 |
|-----|------|
| `Tools/SoulsAssetPipeline/` | git clone 的动画解析库 |
| `Tools/Yabber 1.3.1/Yabber.exe` | 资产解包工具 |
| `Tools/FLVER_Editor/SoulsFormats.dll` | FlverToFbx 引用的 SoulsFormats DLL |

---

## 五、DDS 纹理文件列表

```
AM_M_9000-tpf/
  AM_M_9000_Armor_a.dds          (Albedo)
  AM_M_9000_Armor_m.dds          (Metallic/Specular)
  AM_M_9000_Armor_n.dds          (Normal)
  AM_M_9000_artificialarm_new_a.dds
  AM_M_9000_artificialarm_new_m.dds
  AM_M_9000_artificialarm_new_n.dds

BD_M_9000-tpf/
  BD_M_9000_cloth_a.dds
  BD_M_9000_cloth_n.dds
  BD_M_9000_Court_a.dds
  BD_M_9000_Court_n.dds
  BD_M_9000_tilingset1_new_a.dds
  BD_M_9000_tilingset1_new_n.dds
  BD_M_9000_tops_a.dds
  BD_M_9000_tops_m.dds
  BD_M_9000_tops_n.dds

HD_M_9510-tpf/
  HD_M_9510_dammy_a.dds
  HD_M_9510_dammy_m.dds
  HD_M_9510_dammy_n.dds
  HD_M_9510_dammy_r.dds

LG_M_9000-tpf/
  LG_M_9000_bottoms_new_a.dds
  LG_M_9000_bottoms_new_m.dds
  LG_M_9000_bottoms_new_n.dds
  LG_M_9000_fray_new_a.dds
  LG_M_9000_fray_new_n.dds
  LG_M_9000_tilingbandage_new_a.dds
  LG_M_9000_tilingbandage_new_n.dds
```
纹理后缀含义: `_a` = Albedo, `_n` = Normal, `_m` = Metallic/Specular, `_r` = Roughness

---

## 六、动画包清单 (44个, 共1438个HKX)

| 动画包 | HKX数量 | 说明 |
|-------|---------|------|
| c0000.anibnd.dcx | 1 | 骨架 (skeleton.hkx) |
| c0000_a000_hi | 371 | 主动画-高优先级 |
| c0000_a000_lo | 184 | 主动画-低优先级 |
| c0000_a000_md | 134 | 主动画-中优先级 |
| c0000_a00x | 21 | 通用动画 |
| c0000_a05x | 177 | 武器姿态动画 |
| c0000_a07x | 268 | 额外武器动画 |
| c0000_a2xx | 93 | 杂项动画 |
| c0000_c1020~c1500 | ~82 | 各武器战斗动画 (19个包) |
| c0000_c5000~c5400 | ~76 | 义手忍具动画 (11个包) |
| c0000_c7000~c7400 | ~19 | 特殊动画 (6个包) |

HKX 文件命名规则: `a000_000000.hkx` 其中 `a000` 是动画组, `000000` 是动画 ID。
