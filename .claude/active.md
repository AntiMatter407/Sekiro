# active — Sekiro → UE5.2 迁移进度

> **active.md 是本项目唯一的进度文件。** 其他文件（memory/、docs/ 等）仅作参考，进度以本文件为准。

## 当前阶段
SekiroImport C++ 插件 Phase 4 完成，Phase 5 待实施

## 正在进行
- [x] Phase 4: 材质构建对齐
- [ ] Phase 5: 管线编排

## 方案状态

| 方案 | 状态 | 说明 |
|------|------|------|
| 方案A: Blender FLVER→FBX | ❌ 已取消 (2026-06-08) | C# + Blender 中间步骤，已被方案B取代 |
| 方案B: SekiroImport C++ | ▶ 进行中 | JSON→UE资产直接导入，Phase 1+2+3 完成 |

## SekiroImport 实施计划

> 详见 `Docs/implementation-plan.md`

| Phase | 内容 | 状态 |
|-------|------|------|
| Phase 0 | Python可调用验证框架 | ✅ 完成 |
| Phase 1 | 骨架构建对齐 | ✅ 完成 (147/147骨骼<0.05cm) |
| Phase 2 | 网格构建对齐 | ✅ 完成 (顶点对齐<0.001mm) |
| Phase 3 | 动画构建对齐 | ✅ 完成 (3-Pass FK+OrientQ+DeriveLocal) |
| **Phase 4** | **材质构建对齐** | ✅ **完成 (MTD→BlendMode推导+父材质M_SekiroBase+46MIC)** |
| Phase 5 | 管线编排 | ⏳ 待实施 |

### Phase 3 实现总结 (2026-06-08)

**核心算法**（对齐 Phase 1 `ApplyExportRootOrientation`）：
```
对每帧 t:
  Pass 1: FK in HKX → WorldHKX[i] = LocalHKX[i] * ParentWorldHKX
  Pass 2: OrientQ → WorldUE[i] = OrientQ * WorldHKX[i]
  Pass 3: Derive Local → LocalUE[i] = WorldUE[i].GetRelativeTransform(ParentWorldUE)
  Write LocalUE[i] to AnimSequence curve
```

**修改文件 (6个)**：
1. `SekiroStreamReader.cpp` — 修复 Y-up→Z-up 坐标不一致 (前置)
2. `SekiroImportData.h` — `FSekiroAnimationClip` 新增 `ReferenceLocalTransforms`
3. `SekiroAnimationParser.h/.cpp` — `ParseAnimationClip` 填充参考变换
4. `SekiroAnimationBuilder.cpp` — 完全重写，3-Pass 算法
5. `SekiroImportTest.cpp` — `DumpAnimTracks` 重写，输出 UE 空间 Local 变换

**已验证 (2026-06-08)**：`ParseAnimationAndDump` 输出 3 个动画（4526 行/31帧×146骨骼），3-Pass 算法正确 — 0 NaN、0 退化四元数、FK 世界位置合理

### Phase 4 实现总结 (2026-06-08)

**核心机制**（对齐 Blender 管线 `common_blender.py` CLOTH_KEYWORDS）：
```
MTD→BlendMode推导:
  if 材质名含{cloth,fray,tiling,bandage,muffler,rope,skirt,cape,hair} 或 MTD名含cloth → Masked
  elif MTD名含decal（不含cloth）→ Translucent
  else → Opaque

TwoSided: is_cloth OR is_decal

父材质 M_SekiroBase: 4个TextureSampleParameter2D（_a→BaseColor, _n→Normal, _m→Metallic, _r→Roughness）
46个 MIC 实例: 继承 M_SekiroBase + BasePropertyOverrides(BlendMode+TwoSided)
```

**修改文件 (3个)**：
1. `SekiroMaterialBuilder.cpp` — 完全重写（~410行）：EnsureBaseMaterial + LoadMaterialsJson + BuildSingleMIC + BuildAll
2. `SekiroModelParser.cpp` — ParseMaterials 增加 MTD→BlendMode 回退推导
3. `SekiroImportTest.h/.cpp` — 新增 `BuildMaterialsAndDump` 测试函数

**已验证 (2026-06-08)**：
- BlendMode: 20 Masked, 2 Translucent, 24 Opaque（全部对齐 Blender 管线规则）
- TwoSided: 22 材质 TwoSided=Y (cloth/decal), 24 TwoSided=N
- 46/46 MIC 创建成功，Content Validation 无编译错误
- 父材质 M_SekiroBase 创建成功（/Game/Characters/Sekiro/Materials）

**修复的 bug**：`static const TArray<FString>` → `static const TCHAR*[]` 避免 UE 静态初始化期崩溃

**诊断代码修复 (2026-06-08)**：
- `SekiroSkeletalMeshBuilder.cpp` 顶点诊断：Y-up顶点 vs Z-up骨骼 → 统一施加MeshOrientQ后对比，距离从~196cm降至4-13cm（正常蒙皮范围）
- LOD诊断：不再假设LOD索引=ImportData索引（Build()会merge同材质Section：50→38段，wedge膨胀2.73x）
- 非fray段顶点距骨骼均<50cm，62个>100cm的全来自fray段（Blender数据相同，正确跳过）
- 父材质M_SekiroBase的SamplerType已全改为Color（无编译警告）
- `BuildFullModel` 产出：147骨骼 + 341,561 LOD顶点（38段） + 46 MIC → `/Game/SekiroTest/TestMesh`

## 待办
- [ ] Phase 5: 管线编排
- [ ] UE5.2 项目创建 & UnLua 集成
- [ ] 核心战斗系统设计

## 已完成
- 2026-05-30: 项目启动，技术栈确定（UE5.2 + C++ + UnLua）
- 2026-05-31: 角色模型合并管线建立（5部件→OBJ+JSON→FBX）
- 2026-06-01: FlverToFbx 管线建立（C# + Blender）
- 2026-06-03: SekiroImport Phase 1+2 完成（骨架+网格构建对齐Blender管线）
- 2026-06-08: 方案A取消；Phase 3 完成（动画构建 3-Pass 算法）
- 2026-06-08: Phase 4 完成（材质构建对齐：MTD→BlendMode推导 + 父材质 + 46MIC）

## 阻塞项
- 无
