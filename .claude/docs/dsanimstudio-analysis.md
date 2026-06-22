# DSAnimStudio 核心架构分析

> 分析时间：2026-06-19
> 分析范围：`Tools/DSAnimStudio-master/DSAnimStudioNETCore/` 核心源码 + 已发布的 `DSAS_4.9.9` 版配置

---

## 一、整体架构层次

```
┌──────────────────────────────────────────────────────────────────┐
│  渲染层 (MonoGame + ImGui)                                        │
│  ImguiOSD/Window.*.cs — 所有编辑器窗口                              │
│  NewGraph.cs — TAE 事件图渲染                                       │
│  ViewportEnvironment.cs, WorldView.cs — 3D 视口                    │
├──────────────────────────────────────────────────────────────────┤
│  UI/交互层 (WinForms + ImGui)                                      │
│  TaeEditorScreen.cs — TAE 编辑器主控                                │
│  TaeExportAllAnimsForm.cs — 批量导出对话框                          │
│  Dialog.*.cs — 各种对话框                                           │
├──────────────────────────────────────────────────────────────────┤
│  运行时动画引擎                                                    │
│  NewAnimationContainer  →  动画容器 + 骨骼 + 动画插槽系统           │
│  NewAnimSlot            →  动画插槽 (Base/UpperBody/Additive)       │
│  NewHavokAnimation      →  动画播放状态 (时间/循环/根运动)          │
│  NewAnimSkeleton_HKX    →  HKX 骨骼加载 + FK 计算                  │
│  NewAnimSkeleton        →  骨骼基类 (权重/混合/骨骼映射)            │
│  NewBlendableTransform  →  可混合骨骼变换 (Lerp/Slerp)             │
├──────────────────────────────────────────────────────────────────┤
│  数据管理层                                                        │
│  NewTaeContainer       — TAE 文件集合                              │
│  DSAProj               — DSAnimStudio 项目文件                     │
│  NewChrAsm             — 角色装配 (装备→武器→TAE)                  │
│  NewSkeletonMapper     — HKX↔FLVER 骨骼映射                        │
│  TaeProjectJson.cs     — 项目 JSON 序列化                          │
│  RootMotionDataPlayer  — 根运动数据播放器                          │
├──────────────────────────────────────────────────────────────────┤
│  底层库 (外部的 SoulsAssetPipeline + SoulsFormats)                  │
│  HKX.*                — Havok 二进制格式解析/写入                  │
│  TAE                  — TAE 格式解析                               │
│  HavokAnimationData   — 骨骼变换数据抽象 (Spline/Uncompressed)     │
│  HavokSplineFixer     — Havok 样条曲线修复                        │
│  HavokXmlWriter       — HKX→XML 序列化                            │
│  HavokDowngrade       — HKX 版本升级 (2010→2015)                  │
│  SoulsFormats         — FLVER/BND/TPF/Param 等格式解析             │
├──────────────────────────────────────────────────────────────────┤
│  工具层                                                            │
│  ToolExportAllAnims.cs           — HKX 导出 (XML/Packfile)        │
│  ImportedAnimationConverter.cs   — 导入动画→HKX 格式转换          │
│  SapImportFbxAnimForm.cs         — FBX 动画导入 UI                │
└──────────────────────────────────────────────────────────────────┘
```

---

## 二、TAE 文件结构

### 2.1 层次结构

```
TAE (根)
├── Header (版本、骨骼类型等)
├── Animation 列表
│   ├── Animation ID (唯一标识)
│   ├── AnimFileHeader
│   │   ├── ImportsHKX 标志
│   │   ├── ImportHKXSourceAnimID (引用另一个TAE动画的HKX)
│   │   ├── HKX 文件引用
│   │   └── HKX 绑定信息
│   ├── 轨道 (Track) 列表
│   │   ├── Track ID
│   │   ├── Track Name
│   │   └── Event 列表
│   │       ├── Event ID (事件类型)
│   │       ├── StartTime (帧)
│   │       └── Event 参数数据
│   └── (动画参数: 时长、帧率、循环等)
└── (全局参数/模板引用)
```

### 2.2 事件类型分类

根据 `Res/TAE.Template.SDT.xml` 配置，事件类型包括但不限于：

| 类别 | 事件类型 | 说明 |
|------|---------|------|
| 移动 | 0-99 | 位移、转向、根运动控制 |
| 攻击 | 100-199 | 攻击判定框、伤害参数、武器碰撞 |
| 特效 | 200-299 | 粒子特效、武器特效、击中特效 |
| 声音 | 300-399 | 音效、语音、脚步声 |
| 相机 | 400-499 | 相机震动、FOV 变化、锁定控制 |
| 武器 | 500-599 | 武器切换、弹仓、投射物 |
| 物理 | 600-699 | 布娃娃、刚体、碰撞 |
| 角色 | 700-799 | 脸模表情、AABB/碰撞体 |
| 导航 | 900-999 | Navigation 网格、寻路 |

### 2.3 TAE ↔ HKX 关联机制

```
TAE.Animation
  └── AnimFileHeader
        ├── 如果 ImportsHKX == true
        │     → ImportHKXSourceAnimID 指向另一个 TAE Animation
        │        (那个 Animation 引用实际的 HKX)
        └── 如果 ImportsHKX == false
              → 直接指向 anibnd 中的 HKX 文件
                   └── HKX 内 hkaAnimationBinding
                         └── TransformTrackToBoneIndices[]
                               └── 映射到 hkaSkeleton.Bones[]
```

### 2.4 武器动画的特殊处理

`NewChrAsmWpnTaeManager` 管理武器的额外 TAE 动画：
- 人物角色使用 `c0000.anibnd`
- 武器可能有独立的 TAE 和动画文件
- `EquipmentTaeManager` 将武器动画与角色基础动画叠加

---

## 三、动画系统

### 3.1 骨骼加载流程

```
LoadBaseANIBND(anibnd)
  │
  ├── 从 anibnd 提取骨骼 HKX (ID=1000000 或 4000000)
  │   (DS1/DES: 1000000; DS3/SDT/ER: 4000000)
  │
  ├── 解析 HKX → HKASkeleton
  │   ├── Name (骨骼名称)
  │   ├── ParentIndices (父骨骼索引)
  │   ├── Transforms (参考位姿: Position/Rotation/Scale)
  │   ├── Bones (名称 + lockTranslation)
  │   └── ReferencePose (完整参考位姿矩阵)
  │
  ├── LoadHKXSkeleton(skeletonHkxParsed)
  │   ├── 遍历每个骨骼 → NewBone
  │   │   ├── Index, Name, ParentIndex
  │   │   ├── ReferenceLocalTransform (Position/Rotation/Scale 四元组)
  │   │   └── ReferenceLocalMatrix (矩阵形式)
  │   └── InitBoneTree()
  │       ├── 建立 ChildIndices/ChildBones
  │       ├── 计算 ReferenceFKMatrix (累积到根)
  │       └── 标记 UpperBody/SekiroFace 骨骼
  │
  └── 提取所有动画 HKX (从 anibnd 文件)
      ├── 使用 SplitAnimID 索引 (mod 1_000_000000)
      └── 缓存到 animHKXsToLoad 字典
```

### 3.2 骨骼层级数据结构

```
NewBone
├── Index          : int           (骨骼索引编号)
├── Name           : string         (骨骼名称)
├── ParentIndex    : short          (父骨骼索引, -1为根)
├── ChildIndices   : List<int>      (子骨骼索引列表)
├── Masks          : BoneMasks      (None/UpperBody/SekiroFace)
│
├── ReferenceLocalTransform : NewBlendableTransform
│   (骨骼在绑定姿态下的局部变换: T/R/S)
│
├── ReferenceLocalMatrix : Matrix
│   (局部矩阵形式)
│
├── ReferenceFKMatrix : Matrix
│   (参考姿态下的全局FK矩阵)
│
├── LocalTransform : NewBlendableTransform
│   (动画驱动后的局部变换, 每帧更新)
│
├── FKMatrix       : Matrix
│   (动画驱动后的全局FK矩阵, 每帧更新)
│
├── Weight         : float          (全局权重系数)
│
├── MapToOtherBoneIndex : int      (映射到FLVER骨骼的索引)
│
├── IsNub          : bool           (是否为Nub骨骼)
│
└── MasterCompleteOverrideFK : Matrix?
    (完全覆盖的FK矩阵, 调试用)
```

### 3.3 动画加载流程

```
LoadAnimHKX(hkxBytes, animID, name)
  │
  ├── 解析 HKX 对象
  │   ├── 如果是 Tagfile 格式 → HKX.GenFakeFromTagFile()
  │   └── 如果是 Legacy 格式 → HKX.Read() (支持DS1R hotfix)
  │
  ├── 提取 HKX DataSection Objects:
  │   ├── HKASplineCompressedAnimation    (Spline 压缩)
  │   ├── HKAInterleavedUncompressedAnimation (非压缩)
  │   ├── HKAAnimationBinding             (骨骼↔轨道映射)
  │   └── HKADefaultAnimatedReferenceFrame (根运动数据)
  │
  └── 创建对应的 NewHavokAnimation 子类实例
      ├── NewHavokAnimation_SplineCompressed
      │   └── 持有 HavokAnimationData_SplineCompressed
      │
      └── NewHavokAnimation_InterleavedUncompressed
          └── 持有 HavokAnimationData_InterleavedUncompressed
```

### 3.4 关键帧数据格式

**Spline Compressed 格式：**
```
HKASplineCompressedAnimation
├── Duration                : float (动画总时长, 秒)
├── FrameDuration           : float (每帧时长, 通常 1/30)
├── FrameCount              : int
├── TransformTrackCount     : int (变换轨道数)
├── BlockCount              : int
├── FramesPerBlock          : int
├── BlockDuration           : float
├── BlockOffsets[]          : uint (每个block的数据偏移)
├── Data[]                  : byte (压缩数据)
├── MaskAndQuantization     : uint (旋转量化参数)
│
└── 样条数据格式 (每轨道):
    ├── 位移: 三次样条控制点 (Vector3)
    ├── 旋转: 量化四元数 (THREECOMP40/48)
    └── 缩放: 三次样条控制点 (Vector3)
```

**Interleaved Uncompressed 格式：**
```
HKAInterleavedUncompressedAnimation
├── Duration                : float
├── FrameDuration           : float
├── FrameCount              : int
├── TransformTrackCount     : int
├── Transforms[]            : Transform[] (每帧每轨道的完整变换)
│   └── Transform
│       ├── Translation     : Vector4
│       ├── Rotation        : Quaternion
│       └── Scale           : Vector4
└── (所有轨道的所有帧在内存中连续排列)
```

**绑定映射：**
```
HKAAnimationBinding
├── OriginalSkeletonName    : string
├── TransformTrackToBoneIndices[] : short[] (轨道→骨骼索引映射)
├── BlendHint               : AnimationBlendHint (普通/相加)
└── (部分骨骼可能没有动画轨道映射)
```

### 3.5 FK 计算与骨骼混合

```
NewAnimSkeleton.CalculateFKFromLocalTransforms(getLocalTrack)
  │
  └── WalkTree (递归遍历骨骼树)
      │
      ├── getLocalTrack(i, parentMatrix, scaleMatrix)
      │   → 返回骨骼 i 的本地变换
      │
      ├── 权重混合:
      │   LocalTransform = Lerp(ReferencePose, AnimationTransform, Weight)
      │
      ├── 构建 FK 矩阵:
      │   WorldMatrix = LocalMatrix * ParentWorldMatrix
      │
      └── 骨骼映射 (如果启用了 FLVER 映射):
          FKMatrix = (Lerp(refPose, otherFK, weight))
```

### 3.6 动画插槽混合系统

```
NewAnimationContainer
  └── NewAnimSlots (插槽字典)
      │
      ├── Base (标准混合)
      │   └── 主动画层
      │
      ├── UpperBody (标准混合 + 上身骨骼遮罩)
      │   └── 上半身覆盖动画
      │
      ├── SekiroFace (标准混合 + 面部骨骼遮罩)
      │
      ├── TaeExtraAnim0..10 (相加混合, RelativeToTPose)
      │   └── TAE 触发的附加动画 (最多11层)
      │
      ├── EldenRingHandPose (固定混合模式)
      │
      └── DebugNormal1/DebugAddRelativeToTpose1/... (调试用)

每个 NewAnimSlot 可以包含多层动画 (Foreground + Background 过渡):
  └── 多层动画叠加:
      foreach slot:
        foreach animLayer:
          weight = slot.SlotWeight * animLayer.Weight
          result = Lerp(result, slot.GetBoneTransform(), weight)
```

---

## 四、TAE→HKX 导出管线

### 4.1 导出流程

```
ExportHKX(animContainer, hkxInfo, skeletonHKX, fileType)
  │
  ├── 步骤1: 获取 HKX 结构
  │   animContainer.GetHkxStructOfAnim(hkxBytes, compendiumBytes)
  │   → 解析 HKX 二进制 → HKX 对象
  │
  ├── 步骤2: 选择导出格式
  │
  ├── Havok2010_2_XML:
  │   HavokXmlWriter.WriteHavokPackfileToXML(hkx, skeletonHKX)
  │   → 返回 XML 字节数组
  │
  ├── Havok2010_2_Packfile_x32:
  │   │  XML → 外部 CompressAnim.exe 压缩
  │   │  (包含旋转量化: THREECOMP40, tolerance: 0.001f)
  │   └── 返回压缩后的 HKX 字节数组
  │
  └── Havok2016_1_Tagfile_x64:
      │  XML → 2010 Packfile → HavokDowngrade.UpgradeHkx2010to2015()
      └── 返回 2016 Tagfile 字节数组
```

### 4.2 骨骼导出

```
writeHkxSkeletonTo2010Xml(skeleton)
  → 生成 hk_2010.2.0-r1 格式的 XML:
    └── hkpackfile
        ├── hkRootLevelContainer
        │   └── namedVariant → hkaAnimationContainer
        └── hkaAnimationContainer
            ├── skeletons[1] → hkaSkeleton
            │   ├── name = "Master"
            │   ├── parentIndices[]
            │   ├── bones[] (name + lockTranslation)
            │   ├── referencePose[] (T/R/S)
            │   └── referenceFloats, floatSlots, localFrames = empty
            ├── animations, bindings, attachments, skins = empty
            └── (导出骨骼时不需要动画数据)
```

### 4.3 导入动画转换

```
ImportedAnimationConverter.GetAnimReadyToPutIntoGameFromImported(importedAnim)
  │
  ├── 步骤1: importedAnim.WriteToSplineCompressedHKX2010Bytes()
  │   → 使用 CompressAnim.exe 压缩为 2010 HKX
  │   → 输出 Data2010
  │
  └── 步骤2: 根据目标游戏版本转换:
      ├── DS1: 直接使用 Data2010
      ├── DS1R: HavokDowngrade.UpgradeHkx2010to2015(Data2010)
      └── DS3/SDT/ER: 通过 HKXtoHKX2() 转换为 Tagfile
```

---

## 五、工具链数据流

### 5.1 从游戏目录到编辑器

```
用户选择 → 游戏根目录 (如 D:\SteamLibrary\steamapps\common\Sekiro)
  │
  ├── 读取 regulation.bin → 解密 → Param 数据
  │   (AtkParam/BehaviorParam/EquipParamWeapon 等)
  │
  ├── 从 mapbnd 或 partsbnd 载入 FLVER 模型
  │
  ├── 从 chr 目录载入 anibnd
  │   ├── c0000.anibnd → 主角骨骼 + 动画
  │   ├── 提取骨骼 HKX → LoadHKXSkeleton()
  │   └── 提取所有动画 HKX → AddAnimHKXFetch()
  │
  └── 从 chr 目录载入 TAE
      ├── c0000.tae → TAE 事件数据
      └── 通过 DSAProj 项目文件组织
```

### 5.2 运行时动画播放

```
用户选择动画 (点击 TAE 列表)
  │
  ├── NewSetSlotRequest(BaseSlot, request)
  │   ├── 解析 TAE AnimID → 获取 HKX ID
  │   ├── FindAnimation(hkxID) → 加载/缓存
  │   └── 设置到 Base 插槽
  │
  ├── Update()
  │   └── Scrub(时间增量)
  │       ├── 所有插槽推进时间
  │       ├── 计算根运动 (RootMotion)
  │       └── 计算骨骼 FK → 更新渲染
  │
  └── Draw()
      └── 渲染骨骼 + 模型 + 调试信息
```

### 5.3 资产加载路径

```
anibnd (BND/BHD 容器)
  │
  ├── 文件 ID 范围:
  │   DS1/DES:       0 ~ 255_9999
  │   DS3/SDT/ER:    1000000000 ~ 1999_999999
  │
  ├── 骨骼:    ID=1000000 或 4000000
  │   └── 用 ver_0001 标志判断 (anibnd内有ID=9999999文件)
  │
  ├── 动画:    ID % 1_000_000000 作为 SplitAnimID
  │
  └── Compendium: ID=7000000 (Tagfile 格式专用)
      └── 配合 HKX.GenFakeFromTagFile() 使用
```

---

## 六、关键设计决策总结

| 决策 | 说明 |
|------|------|
| **实时引擎 + 编辑器合一** | DSAnimStudio 不是纯导出工具，而是完整的动画播放/编辑运行时 |
| **Havok 为中间格式** | 所有动画走 HKX 解析→修改→重压缩，不直接操作骨骼变换数组 |
| **SAP 库封装底层** | HKX/TAE/FLVER 格式解析在 SoulsAssetPipeline 库中，DSAnimStudio.NETCore 只引用接口 |
| **骨骼双系统** | HKX 骨骼 (动画驱动) + FLVER 骨骼 (渲染驱动)，通过映射同步 |
| **插槽混合架构** | 11 层叠加动画 + 骨骼遮罩 + 多种混合模式，模拟游戏内动画混合 |
| **游戏版本兼容** | 支持 DS1/DES/DS2/BB/DS3/SDT/ER/AC6 等不同游戏的不同 HKX 格式 |
| **外部压缩工具** | CompressAnim.exe 处理 Havok 样条压缩，不在源码内 |
| **根运动四元组** | RootMotion 用 Vector4 (XZ位移, Y位移, 旋转W) 表示，而不是完整矩阵 |

---

## 七、与 Sekiro→UE5 项目的关系

DSAnimStudio 对我们项目的参考价值：

### 可复用的设计

1. **骨骼加载逻辑**：`NewAnimSkeleton_HKX.LoadHKXSkeleton` 展示了如何从 HKX 解析骨骼层级
2. **动画帧提取**：`HavokAnimationData` 提供了从 Spline/Uncompressed HKX 提取每帧骨骼变换的接口设计
3. **TAE→HKX 映射**：`NewGetAnimFromRequest` 展示了 TAE Animation ID 到 HKX 数据的完整解引用链
4. **文件 ID 范围**：Sekiro (SDT) 的 anibnd 文件 ID 范围和骨骼 ID 位置

### 不需要照搬的方面

1. **Havok 压缩管线**：我们不需要重新压缩回 HKX，只需要读取后转为 UE 动画
2. **实时混合系统**：11 层插槽混合是编辑器的需求，我们导入到 UE 后由 UE 动画蓝图处理
3. **外部 CompressAnim.exe**：可以直接用 C++/Python 实现 HKX 解析，不依赖外部工具
4. **XNA/MonoGame 渲染**：我们只需要数据层，不需要渲染层

### 关键要解决的问题

1. **HKX 解析**：需要 C++/Python 的 HKX 解析器（SAP 库中的 `SoulsFormats.HKX` 类）
2. **Spline 解压缩**：Havok 样条曲线 → 线性帧的转换算法
3. **坐标系转换**：DS 坐标系 (Y-up) → UE 坐标系 (Z-up) 的骨骼变换转换
4. **骨骼命名对齐**：HKX 骨骼名称到 UE 骨骼名称的映射
