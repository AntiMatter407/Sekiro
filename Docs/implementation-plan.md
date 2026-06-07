# SekiroImport 重构：复刻 JSON→FBX→UE 管线逻辑

## Context

两套管线产生不同结果：
1. **Blender/FBX管线**（`Tools/FlverToFbx/common_blender.py`）：JSON → Blender → FBX → UE5
2. **C++ SekiroImport**：JSON → 直接UE资产，算法不同

目标：C++插件**完全对齐**Blender管线逻辑，省略FBX中间文件。

## 坐标策略

全程 Y-up，最终施加 `ExportRoot(Z=180°) + Armature(X=90°) + Scale(×100)` 变换：
```
M_final = Scale(100) * RotZ(180°) * RotX(90°)
Vector: apply full M_final
Quat:   apply RotZ(180°) * RotX(90°) only
```

---

## 阶段零：Python可调用验证框架（先行基础设施）

**目标**：将C++核心函数暴露为UE Python可调用API，每个函数输出诊断文件，方便与Blender管线逐项对比。

### 新建文件：`SekiroImportTest.h/.cpp`

创建 `USekiroImportTest` BlueprintFunctionLibrary，暴露以下函数供Python调用：

```cpp
UCLASS()
class USekiroImportTest : public UBlueprintFunctionLibrary
{
    // 解析 + 输出对比文件
    UFUNCTION(BlueprintCallable) static bool ParseModelAndDump(const FString& JsonPath, const FString& OutputDir);
    UFUNCTION(BlueprintCallable) static bool ParseAnimationAndDump(const FString& JsonPath, const FString& OutputDir);
    
    // 构建 + 输出诊断文件
    UFUNCTION(BlueprintCallable) static bool BuildSkeletonAndDump(const FString& ModelJson, const FString& AnimJson, const FString& OutputDir);
    UFUNCTION(BlueprintCallable) static bool BuildMeshAndDump(const FString& ModelJson, const FString& SkeletonPath, const FString& OutputDir);
    UFUNCTION(BlueprintCallable) static bool BuildAnimationAndDump(const FString& AnimJson, const FString& SkeletonPath, int32 ClipIndex, const FString& OutputDir);
    
    // 全流程对比入口
    UFUNCTION(BlueprintCallable) static bool RunFullPipelineAndDump(const FString& ModelJson, const FString& AnimJson, const FString& OutputDir);
};
```

### 输出文件格式（与现有诊断文件兼容）

| 文件 | 格式 | 内容 |
|------|------|------|
| `Bones_Cpp.txt` | `idx\|name\|parentIdx\|posX\|posY\|posZ` | FK世界空间骨骼位置 |
| `Verts_Cpp.txt` | `sec\|vertIdx\|posX\|posY\|posZ\|localBone\|boneName\|skelIdx\|weight\|bonePosX\|bonePosY\|bonePosZ` | 顶点蒙皮数据 |
| `RefSkel_Cpp.txt` | `idx\|name\|parentIdx\|locX\|locY\|locZ\|rotX\|rotY\|rotZ\|rotW` | 参考骨架变换 |
| `Anim_Cpp.txt` | `clipName\|frame\|boneIdx\|boneName\|posX\|posY\|posZ\|rotX\|rotY\|rotZ\|rotW` | 动画曲线数据 |

### Python对比脚本：`Tools/compare_pipeline.py`

```python
"""对比 C++ 管线 vs Blender 管线输出"""
# 用法: UE5 Python控制台中
# import compare_pipeline; compare_pipeline.compare_skeleton()
# 或: compare_pipeline.compare_mesh()
# 或: compare_pipeline.compare_all()
```

### 验证工作流
```
[修改C++代码] → [UE5中运行Python: compare_pipeline.compare_xxx()] → [查看差异报告] → [修复] → [重复]
```

---

## 阶段一：骨架构建对齐

**目标**：`Bones_Cpp.txt` 中FK位置与FBX骨骼位置差异 < 0.01cm

### 改造文件
- `SekiroModelParser.cpp` — 移除坐标轴旋转转换，保留m→cm缩放
- `SekiroAnimationParser.cpp` — 同上
- `SekiroSkeletonBuilder.cpp` — 重写，对齐 `common_blender.py:build_armature()`

### 核心算法
```
BuildUnionSkeleton(anim_data, model_data):
  1. 动画骨骼FK: world_mat = parent_world @ local_mat
  2. 构建: head=world_pos, rotation=world_rot  
  3. 追加Model-Only骨骼
  4. 父关系: 动画骨骼用HKX ParentIndex, Model-Only用ParentName
  5. 计算Local transform: FTransform::GetRelativeTransform(parentWorld)
  6. 施加ExportRoot旋转: RotZ(180°) * RotX(90°) * localTrans
```

### 验证
```python
import compare_pipeline
compare_pipeline.compare_skeleton()  
# 期望: "17/17 骨骼FK位置差异 < 0.01cm"
```

---

## 阶段二：网格构建对齐 ✅ 已确认，待实施

**目标**：顶点位置/法线与骨架在同一坐标空间（UE5 ExportRoot旋转后），三角形绕序对齐Blender

**已完成探索结论**：
- UV V-flip **不需要** — UE5与FLVER同为左上角原点
- M_final缩放已在Parser施加（m→cm），Build阶段仅需OrientQ旋转
- Blender管线不直接旋转顶点（骨架通过armature modifier施加变换），C++管线需显式旋转顶点
- 蒙皮映射（LocalBoneIndex→BoneName→SkelIndex）已正确，无需改动

### 改造文件

**1. `SekiroSkeletalMeshBuilder.cpp` — 5处修改**

| # | 位置 | 变更 |
|---|------|------|
| 1 | 行236-239 | 顶点位置施加 `OrientQ.RotateVector()` |
| 2 | 行236-239 | 顶点法线施加 `OrientQ.RotateVector()` 后归一化 |
| 3 | 行225-233 | Decal材质检测：获取MTD path，判断含"decal" |
| 4 | 行236-239 | Decal顶点沿法线偏移0.08cm（在OrientQ旋转前） |
| 5 | 行260-279 | 三角形绕序反转：`(Tri.X, Tri.Y, Tri.Z)` → `(Tri.X, Tri.Z, Tri.Y)` |

**OrientQ定义**（与 `SekiroSkeletonBuilder.cpp` 行195 一致）：
```cpp
static const FQuat OrientQ = FQuat(FVector(0, 0, 1), PI) * FQuat(FVector(1, 0, 0), PI / 2.0);
```

**Decal检测逻辑**：
```cpp
// 在Section循环内，从ModelData.Materials[Section.MaterialIndex]获取MTDPath
bool bIsDecal = false;
if (Section.MaterialIndex >= 0 && Section.MaterialIndex < ModelData.Materials.Num())
    bIsDecal = ModelData.Materials[Section.MaterialIndex].MTDPath.ToLower().Contains(TEXT("decal"));
```

**顶点处理伪代码**（替换行236-239）：
```cpp
for (const FSekiroImportVertex& Vert : Section.Vertices)
{
    FVector3f Pos = Vert.Position;
    if (bIsDecal)
        Pos += Vert.Normal * 0.08f;  // 0.08cm = 0.0008m × 100(cm/m)
    Pos = FVector3f(OrientQ.RotateVector(FVector(Pos)));
    ImportData.Points.Add(Pos);
    
    FVector3f Normal = FVector3f(OrientQ.RotateVector(FVector(Vert.Normal)).GetSafeNormal());
    ImportData.Normals.Add(Normal);  // 需要启用法线导入
}
```

**2. `SekiroImportTest.cpp` — 1处修改**

| # | 位置 | 变更 |
|---|------|------|
| 1 | `DumpVertSkinning` 行125 | 顶点位置 VPos 施加 `OrientQ.RotateVector()` 使诊断输出与骨架统一空间 |

**OrientQ在Test文件中的定义**（与Builder一致）：
```cpp
static const FQuat DiagOrientQ = FQuat(FVector(0, 0, 1), PI) * FQuat(FVector(1, 0, 0), PI / 2.0);
```

### 验证

手动检查 `Saved/Logs/SekiroSkinDiag/Verts_Cpp.txt`：
- 顶点位置应与 `Bones_Cpp.txt` 骨骼FK位置在同一坐标空间
- 三角形绕序反转后，LOD诊断不应出现顶点合并错误
- Decal Section的顶点应略微沿法线偏移

---

## 阶段三：动画构建对齐

**目标**：动画曲线数据与FBX导入的AnimSequence一致

### 改造文件
- `SekiroAnimationBuilder.cpp` — Delta变换模式

### 核心算法（对齐 `common_blender.py:add_actions()`）
```
delta_mat = ref_mat.inverse() * frm_mat
delta_loc, delta_rot, delta_scale = delta_mat.decompose()
// delta_rot施加ExportRoot旋转修正
```

### 验证
```python
compare_pipeline.compare_animation("a000")
# 期望: "所有帧的骨骼变换差异 < 0.001"
```

---

## 阶段四：材质构建对齐

### 改造文件
- `SekiroMaterialBuilder.cpp` — 对齐 `common_blender.py:create_materials()`

### 关键变更
1. BlendMode: Decal→Translucent, Cloth→Masked, Other→Opaque
2. TwoSided: Decal + Cloth
3. 纹理匹配: 后缀(_a/_n/_m/_r) + Part前缀
4. 创建 UMaterial 替代 MaterialInstance

---

## 阶段五：管线编排

更新 `SekiroImportPipeline.cpp` 整合所有重构后的Builder。

---

## 文件变更总览

| 文件 | 操作 | 阶段 |
|------|------|------|
| `Public/SekiroImportTest.h` | **新建** | 0 |
| `Private/SekiroImportTest.cpp` | **新建** | 0 |
| `Tools/compare_pipeline.py` | **新建** | 0 |
| `Private/SekiroModelParser.cpp` | 修改 | 1 |
| `Private/SekiroAnimationParser.cpp` | 修改 | 1 |
| `Public/SekiroSkeletonBuilder.h` | 重写 | 1 |
| `Private/SekiroSkeletonBuilder.cpp` | 重写 | 1 |
| `Public/SekiroSkeletalMeshBuilder.h` | 修改 | 2 |
| `Private/SekiroSkeletalMeshBuilder.cpp` | 重写 | 2 |
| `Public/SekiroAnimationBuilder.h` | 修改 | 3 |
| `Private/SekiroAnimationBuilder.cpp` | 重写 | 3 |
| `Public/SekiroMaterialBuilder.h` | 修改 | 4 |
| `Private/SekiroMaterialBuilder.cpp` | 重写 | 4 |
| `Private/SekiroImportPipeline.cpp` | 修改 | 5 |
