<p align="center">
  <h1 align="center">只狼：影逝二度 — UE5.2 迁移项目</h1>
  <p align="center">
    将《只狼：影逝二度》(Sekiro: Shadows Die Twice) 的完整游戏体验迁移到 Unreal Engine 5.2。
    <br />
    C++ 核心 + Lua/UnLua 脚本层 · 48 代理协作架构
  </p>
</p>

---

## 项目目标

复刻只狼的核心玩法，包括：

- **战斗系统**：拼刀、架势条、忍杀、义手忍具
- **移动机制**：钩绳、攀爬、游泳、潜行
- **敌人AI**：BOSS 战行为、杂兵巡逻与警戒
- **资产迁移**：角色、场景、动画从原游戏提取并转换到 UE5.2

## 技术栈

| 层 | 技术 | 说明 |
|---|---|---|
| 引擎 | Unreal Engine 5.2 | 核心游戏框架 |
| 核心开发 | C++ | 角色移动、物理、战斗状态机、AI行为树、动画蓝图、网络同步 |
| 脚本层 | Lua (UnLua) | 技能配置、敌人行为脚本、UI逻辑、任务/对话系统、数据表热加载 |
| 资产管线 | Blender → FBX → UE5 | 从原游戏提取 FLVER/HKX/TPF → Blender 整合 → 导出到 UE |

## 项目结构

```
Sekiro/
├── Source/Sekiro/              ← C++ 游戏模块
│   ├── Core/                   ← 核心框架 (GameMode, GameState, GameInstance)
│   ├── Combat/                 ← 战斗系统 (拼刀、架势条、忍杀)
│   ├── Character/              ← 角色 (玩家、敌人、NPC)
│   ├── AI/                     ← AI 系统 (行为树节点、EQS)
│   ├── Movement/               ← 移动系统 (钩绳、攀爬、游泳)
│   ├── Prosthetic/             ← 义手忍具系统
│   └── UI/                     ← UI 通用组件
├── Content/
│   ├── Script/                 ← Lua 脚本
│   │   ├── Combat/             ← 战斗逻辑 (技能配置、弹刀判定)
│   │   ├── AI/                 ← 敌人行为脚本
│   │   ├── UI/                 ← UI 逻辑
│   │   └── Config/             ← 数据表、数值配置
│   ├── Characters/             ← 角色资产
│   ├── Animations/             ← 动画资产
│   ├── Environments/           ← 场景资产
│   ├── Weapons/                ← 武器资产
│   ├── VFX/                    ← 特效
│   └── Audio/                  ← 音效
├── Extracted/                  ← 从原游戏提取的原始资产
└── .claude/                    ← Claude Code 代理/技能/规则配置
```

## 资产管线

从《只狼》原游戏中提取资产，通过 Blender 标准管线转换到 UE5.2：

1. **模型**：FLVER → [io_scene_flver](https://github.com/Meowmaritus/DSMapStudio) → Blender → FBX → UE5.2
2. **动画**：HKX → [HKXPack](https://github.com/Dasaav-dsv/HKXPack) / HavokTool → SMD → Blender → FBX → UE5.2
3. **纹理**：TPF/DDS → 直接导入 UE5.2

## 开发环境

- **引擎版本**：Unreal Engine 5.2
- **IDE**：Visual Studio 2022 / Rider
- **脚本调试**：UnLua + VSCode (EmmyLua)
- **资产工具**：Blender 4.x + io_scene_flver + DS Map Studio
