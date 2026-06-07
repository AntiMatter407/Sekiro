---
name: project-structure
description: 项目目录结构定义——Source/C++、Content/Script/Lua、Content/资产
type: reference
originSessionId: 9d71e0a4-bf52-4119-b2ed-e434ef408d46
---
## 项目目录结构

```
Sekiro/
├── Source/                    ← C++ 游戏模块
│   └── Sekiro/
│       ├── Sekiro.Build.cs
│       ├── Sekiro.h
│       ├── Sekiro.cpp
│       ├── Core/              ← 核心框架（GameMode, GameState, GameInstance）
│       ├── Combat/            ← 战斗系统（拼刀、架势条、忍杀）
│       ├── Character/         ← 角色（玩家、敌人、NPC）
│       ├── AI/                ← AI 系统（行为树节点、EQS）
│       ├── Movement/          ← 移动系统（钩绳、攀爬、游泳）
│       ├── Prosthetic/        ← 义手忍具系统
│       └── UI/                ← UI 通用组件
├── Content/
│   ├── Script/                ← Lua 脚本
│   │   ├── Main.lua           ← 入口
│   │   ├── Combat/            ← 战斗逻辑（技能配置、弹刀判定）
│   │   ├── AI/                ← 敌人行为脚本
│   │   ├── UI/                ← UI 逻辑
│   │   └── Config/            ← 数据表、数值配置
│   ├── Characters/            ← 角色资产
│   ├── Animations/            ← 动画资产
│   ├── Environments/          ← 场景资产
│   ├── Weapons/               ← 武器资产
│   ├── VFX/                   ← 特效
│   └── Audio/                 ← 音效
└── .claude/
    └── active.md               ← 进度追踪
```

## C++ / Lua 职责划分

| 层 | 负责 |
|---|---|
| C++ | 角色移动、物理、战斗状态机底层、AI 行为树节点、动画蓝图节点、网络同步 |
| Lua  | 技能配置、敌人行为脚本、UI 逻辑、任务/对话系统、数据表热加载 |
