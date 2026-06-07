---
name: project-overview
description: 只狼迁移UE5.2项目——基于UnLua的C++与Lua混合开发
type: project
originSessionId: 9d71e0a4-bf52-4119-b2ed-e434ef408d46
---
将《只狼：影逝二度》(Sekiro: Shadows Die Twice) 的完整游戏体验迁移到 Unreal Engine 5.2。

## 技术栈
- **引擎**: Unreal Engine 5.2
- **核心开发**: C++ (UE Gameplay Framework)
- **脚本层**: Lua via UnLua
- **脚本目录**: Content/Script/
- **C++ 目录**: Source/
- **资产目录**: Content/ (Characters, Animations, Environments, Weapons, VFX, Audio)

## 核心目标
- 复刻只狼的核心战斗系统（拼刀、架势条、忍杀、义手忍具）
- 迁移/重制游戏资产（角色、场景、动画）
- 实现只狼特有的移动和探索机制（钩绳、潜行、游泳）
- 复刻BOSS战和敌人AI系统

## 资产策略

### 方案A: Blender 标准管线 (参考)
- FLVER模型 + HKX动画 → Blender → FBX → UE5.2
- 详见 memory/animation_pipeline.md

### 方案B: C++ SekiroImport 直接导入 (主力推进)
- JSON → C++插件直接创建UE资产，跳过Blender/FBX中间步骤
- 目标：完全对齐Blender管线逻辑，消除中间格式差异
- 进度：
  - ✅ Phase 1: 骨架构建 (147/147骨骼<0.05cm)
  - ✅ Phase 2: 网格构建 (顶点对齐<0.001mm)
  - ⏳ Phase 3: 动画构建
  - ⏳ Phase 4: 材质构建
  - ⏳ Phase 5: 管线编排
- 实现计划: Docs/implementation-plan.md
- 验证框架: SekiroImportTest.h/.cpp (UE Python可调用诊断API)
- 诊断输出: Saved/Logs/SekiroSkinDiag/
- 关键文件:
  - SekiroSkeletonBuilder.cpp (MergeModelWorldTransforms, ApplyExportRootOrientation)
  - SekiroSkeletalMeshBuilder.cpp (OrientQ旋转顶点, Decal偏移, 三角形绕序反转)
  - SekiroImportTest.cpp (DumpBonesFK, DumpVertSkinning, RunFullPipelineAndDump)
