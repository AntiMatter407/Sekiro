# SekiroAssetManager — 资产导入管理器

| 状态 | 创建 | 更新 |
|------|------|------|
| ✅ 已完成 | 2026-06-18 | 2026-06-21 |

## 需求描述

创建 SekiroAssetManager 新模块，支持三个功能块独立导入模型、动画和材质。分 Python 侧（管线编排）和 C++ 侧（UE 资产处理）两层。同时重构现有纹理匹配逻辑，从评分匹配改为使用 FlverToFbx 的 ResolvedMaterials 确定性匹配。

新增：ModelImporter 必须直接解析 FLVER 二进制文件结构导出 JSON，不再依赖外部 C# FlverToFbx 工具。

## 任务树

<!--
  状态图标：⬜ 未开始  🔄 进行中  ✅ 已完成  ❌ 已取消
  依赖标注：任务后追加 (依赖: 1, 2.1)
-->

- ✅ 1. Python 侧 SekiroAssetManager 模块
  - ✅ 1.1 包结构 + pipeline_config
  - ✅ 1.2 ModelImporter — 模型管线
    - ✅ 1.2.1 FlverParser — Python 解析 FLVER 二进制文件
    - ✅ 1.2.2 MtdParser — Python 解析 MTD 二进制文件
    - ✅ 1.2.3 ModelJsonBuilder — 从 FLVER+MTD+TPF 构建模型 JSON
    - ✅ 1.2.4 Output 路径重构 — 使用 Output/<前缀>/{Model,Animation,Material}/
    - ✅ 1.2.5 FK 累积 + skeleton_source HKX 骨架注入 — 修复 WorldPos=LocalPos bug，146根完整骨架（详见 tech-design/sekiro-asset-manager-model-json.md）
  - ✅ 1.3 AnimationImporter — 动画管线
    - ✅ 1.3.1 run_anim_extractor 重构 — 工具内建到 ext_tools，路径指向 Output
    - ✅ 1.3.2 动画名后处理 — 去掉 C# 工具硬编码的 "Sekiro_" 前缀
  - ✅ 1.4 MaterialImporter — 材质管线（基于 ResolvedMaterials）
  - ✅ 1.5 Manager 主类 + CLI（import_all 入口）

- ✅ 2. C++ 侧 SekiroAssetManager 插件
  - ✅ 2.1 插件框架（.uplugin + Build.cs + 空模块）
  - ✅ 2.2 SAModelImporter — JSON → USkeletalMesh（含 ModelOnly追加 + 多根防御 + 三角形过滤）
  - ✅ 2.3 SAAnimationImporter — JSON → UAnimSequence（3-Pass算法, 326个动画导入通过）
  - ✅ 2.4 SAMaterialImporter — JSON → UMaterial（基于 ResolvedMaterials, Build.cs + MaterialEditor）
  - ✅ 2.5 USekiroAssetManager 主类（UFUNCTION 接口）

- ✅ 3. 纹理匹配确定性化
  - ✅ 3.1 common_blender.py assign_textures_globally 优先使用 ResolvedMaterials
  - ✅ 3.2 删除评分匹配代码（score_texture_candidate）
  - ✅ 3.3 BlendMode/TwoSided 直接使用 ResolvedMaterials 值

- ✅ 4. 验证
  - ✅ 4.1 Python 管线验证（fk_accumulate + skeleton_source → 146根/1根根骨骼/WorldPos!=LocalPos）
  - ✅ 4.2 C++ 插件编译验证（UBT 编译通过）
  - ✅ 4.3 全链路验证（SAImport Commandlet: Model 0 error/146骨/20647顶点; Anim 326动画成功导入）

## 变更记录
| 日期 | 变更 |
|------|------|
| 2026-06-18 | 创建 |
| 2026-06-18 | 任务 1 全部完成 |
| 2026-06-18 | 任务 1.2 拆分为子任务 1.2.1~1.2.5（FLVER/MTD/TPF Python 解析） |
| 2026-06-19 | 方案B实施：ModelJsonBuilder skeleton_source + FK 累积修复 WorldPos=LocalPos；SAModelImporter ModelOnly追加 + 多根防御 + 三角形过滤；全链路验证通过 |
| 2026-06-19 | 修复 flver_parser _parse_internal L1390 + _read_vertices_for_mesh L1112 中 _read_vertex 返回值被丢弃的 bug（导致顶点全为零）；ImportedBounds 验证通过 |
| 2026-06-19 | SAAnimationImporter 实现（3-Pass算法 + IAnimationDataController）+ SAMaterialImporter 实现（BlendMode/TwoSided/纹理连接）；公共常量提取到 SAImportData.h 消除重定义；Commandlet 模型/动画/材质三模式独立；326个动画导入验证通过 |
| 2026-06-19 | 纹理匹配确定性化：common_blender.py Pass 0.5 ResolvedMaterials优先 + BlendMode/TwoSided确定性 + 删除score_texture_candidate；DDS→PNG转换管线 + 纹理引用改为PNG |
