<p align="center">
  <h1 align="center">Sekiro → Unreal Engine 5.2</h1>
  <p align="center">
    《只狼：影逝二度》游戏系统迁移与研究项目
    <br />
    C++ 运行时 · Lua / UnLua 玩法与动画声明 · Python 资产管线
  </p>
</p>

> [!IMPORTANT]
> 项目仍在开发中。仓库提供迁移代码、插件和工具，不包含《只狼：影逝二度》的原始游戏资产；使用者需自行合法持有并提取相关数据。

## 项目定位

本项目以 Unreal Engine 5.2 重建《只狼》的角色、战斗、输入、动画和资产工作流。当前重点不是把游戏逻辑全部写死在动画蓝图或 C++ 中，而是形成一套可检查、可编译、可调试的分层架构：

- C++ 提供角色、移动、战斗、动画和编辑器插件的通用能力。
- Lua 通过 UnLua 编排玩法，并以声明式 DSL 描述动画图和强类型 Transition Rule AST。
- `SekiroAnimBlueprintExt` 在编辑器阶段把 Lua 动画描述编译为 UE 原生 AnimGraph；运行时由原生动画节点求值。
- Python 与 `SekiroAssetManager` 负责模型、动画、纹理和配置数据的导入、修复与验证。
- `SekiroAIBridge` 提供编辑器状态查询、编译、资产操作和自动化接口。

## 当前能力

| 领域 | 已有基础 |
|---|---|
| 游戏运行时 | 角色、相机、输入、移动、战斗、武器、UI 与动画模块 |
| Lua 动画蓝图 | 分层动画图、状态机、原生 Transition Rule AST、Root Motion、曲线与 Foot IK |
| 资产管线 | Sekiro 模型描述导入、骨骼网格体、动画、材质和纹理处理脚本 |
| 编辑器自动化 | TCP JSON-RPC 桥接、C++ / Blueprint 编译、资产查询与编辑器控制 |

系统设计和实现状态以 [系统索引](Docs/gdd/systems-index.md) 及各专题文档为准。

## 快速开始

### 环境要求

- Windows 10 / 11
- Unreal Engine 5.2 源码版或已编译版本
- Visual Studio 2022，安装“使用 C++ 的游戏开发”工作负载
- PowerShell 7（推荐）
- Blender、Yabber、DSAnimStudio 等资产工具仅在执行对应管线时需要

仓库已包含项目使用的 UnLua、动画扩展、资产管理和 AI Bridge 插件。先将本机 UE5.2 根目录写入当前 PowerShell 会话：

```powershell
$env:UE_ENGINE_DIR = "F:\UnrealEngine-5.2"
```

生成 Visual Studio 工程文件可直接右键 `Sekiro.uproject`，选择 **Generate Visual Studio project files**。随后编译编辑器目标：

```powershell
& "$env:UE_ENGINE_DIR\Engine\Build\BatchFiles\Build.bat" `
    SekiroEditor Win64 Development `
    -Project="$PWD\Sekiro.uproject" `
    -WaitMutex
```

编译成功后，通过 `Sekiro.uproject` 启动编辑器。若本机项目仍绑定到另一份引擎安装，请先使用 UnrealVersionSelector 重新关联 UE5.2。

## 项目结构

以下目录树只展示 Git 已跟踪、会随仓库同步的内容。`Binaries/`、`Intermediate/`、`Saved/`、`Extracted/`、`Tools/` 等本地目录不在此处列出。

```text
Sekiro/
├── Source/Sekiro/                  C++ 游戏运行时（SK 前缀）
│   ├── Animation/                  动画实例、Root Motion 与动画接口
│   ├── Camera/                     相机系统
│   ├── Character/                  角色基础
│   ├── Combat/                     战斗与防御
│   ├── Core/                       项目核心类型
│   ├── Input/                      Enhanced Input 与输入语义
│   ├── Movement/                   移动系统
│   ├── UI/                         UI 运行时
│   └── Weapon/                     武器系统
├── Content/
│   ├── Script/                     Lua / UnLua 脚本
│   │   ├── Animation/              Lua 动画蓝图、编译器与动画层
│   │   ├── Gameplay/               玩法编排
│   │   └── Debug/                  Lua 调试辅助
│   ├── Characters/                 角色资产
│   ├── Gameplay/                   游戏性资产
│   ├── Input/                      输入资产
│   ├── LevelPrototyping/           关卡原型资产
│   ├── StarterContent/             UE Starter Content
│   ├── ThirdPerson/                Third Person 模板资产
│   └── Weapons/                    武器资产
├── Plugins/
│   ├── SekiroAnimBlueprintExt/     Lua 动画蓝图编译与运行时
│   ├── SekiroAssetManager/         Sekiro 资产导入编辑器插件
│   ├── SekiroAIBridge/             UE 编辑器 TCP JSON-RPC 桥接
│   ├── UnLua/                      Lua 集成
│   └── UnLuaExtensions/            LuaSocket、Protobuf、RapidJSON
├── Script/
│   ├── sekiro_asset_manager/       资产提取、转换与导入管线
│   ├── aibridge/                   AI Bridge 客户端与辅助脚本
│   └── *.py / *.ps1 / *.json       专项管线、修复和验证脚本
├── Docs/                           设计、用法、计划与引擎参考
├── Config/                         UE 项目配置
├── .codex/                         项目规则、技能与协作配置
├── Sekiro.uproject                 UE 项目入口
└── README.md                       仓库入口
```

更完整的说明见 [项目目录结构](Docs/directory-structure.md)。

## 常用入口

| 目标 | 文档 |
|---|---|
| 了解游戏系统 | [系统索引](Docs/gdd/systems-index.md) |
| 实现玩家攻防 | [角色攻击防御系统技术设计](Docs/design/sekiro-attack-defense-system.md) |
| 实现 Boss AI | [Boss AI 系统设计](Docs/gdd/boss-ai-system.md) |
| 编写 Lua 动画蓝图 | [Lua 动画蓝图编写指南](Docs/lua-anim-blueprint-authoring-guide.md) |
| 理解动画编译与运行时 | [Lua AnimGraph 运行时架构](Docs/lua-animgraph-runtime-architecture.md) |
| 编写和验证动画曲线 | [动画曲线制作指南](Docs/animation-curve-authoring-guide.md) |
| 执行资产导入 | [资产导入指南](Docs/sekiro-asset-import.md) |
| 理解完整资产管线 | [资产管线说明](Docs/sekiro-asset-pipeline.md) |
| 使用编辑器自动化接口 | [AIBridge 参考](Docs/aibridge-reference.md) |
| 了解开发协作方式 | [工作流指南](Docs/WORKFLOW-GUIDE.md) |
| 编写项目 Lua | [Lua 代码规范](Docs/lua-code-style.md) |

## 同步与复现状态

当前结论：**Git 已同步内容可以独立编译编辑器，但还不能完整复现当前本地可运行项目。**

已验证：

- 从当前 `HEAD` 只导出 Git 已提交的 `Sekiro.uproject`、`Config/`、`Source/` 和 `Plugins/`。
- 在隔离目录中成功编译 `SekiroEditor Win64 Development`，说明 C++ 游戏模块及已同步插件可以从零构建。
- 仓库未使用 Git LFS；当前 1900 个 `.uasset` / `.umap` 约 1.78 GB，均作为普通 Git 对象同步。

仍需处理：

1. `ABP_Sekiro.lua` 已引用 `CombatBasePose.lua`，但后者尚未被 Git 跟踪；干净同步后会缺少该 Lua 动画状态机。
2. 当前 `Content/Gameplay/Maps/ThirdPersonMap` 地图、BuiltData 和 External Actors 尚未同步；已跟踪的旧启动地图也引用了未同步的 External Actor。
3. `Output/` 和 `Script/sekiro_asset_manager/ext_tools/SoulsAssetPipeline/` 是 Gitlink，但仓库没有 `.gitmodules`；新机器无法自动取得其内容。后者还是多个资产提取工具的直接编译依赖。
4. 当前地图的一个 External Actor 仍引用被忽略的 `AdvancedLocomotionV4` 商城内容。要实现可复现，需要移除该引用，或在环境要求中明确由使用者自行安装相同版本。
5. 当前工作区还有未提交的 Lua 编译器、文档、地图和资产变更；远端只能复现已推送的 `master`，不能复现本机最新状态。

因此目前的复现等级是“**源码可编译**”，尚未达到“**同步后直接打开并运行当前玩法场景**”。

## 后续开发计划

近期目标是完成“狼与一个 Boss 的完整对战闭环”。实现顺序遵循战斗系统依赖，Boss AI 不绕过玩家已经使用的碰撞、攻防、伤害和动画接口。

| 阶段 | 目标 | 主要内容 | 完成标志 |
|---|---|---|---|
| 1. 对战裁决 | 从动画原型升级为真实攻防 | 武器连续 Sweep、攻击上下文、命中去重、Block、Deflect、伤害、HP、架势、硬直和中断 | 玩家与训练假人可完成攻击、防御、弹刀、架势崩坏 |
| 2. 忍杀闭环 | 建立战斗胜负条件 | 忍杀资格、交互距离与朝向、处决动画、死亡和状态清理 | 架势崩坏后能够稳定完成忍杀 |
| 3. Boss AI 行为树 | 让 Boss 使用正式战斗系统决策 | Lua 声明式行为树、目标感知、行动选择与阶段切换 | Boss 能根据距离和战况自主完成攻防循环 |
| 4. Boss 阶段系统 | 支持多阶段战斗 | 阶段数据、招式池切换、权重调整、危字攻击、阶段演出和最终死亡 | 单个 Boss 至少完成两个阶段的连续战斗 |
| 5. 对战表现与调优 | 形成可玩的 Boss 战 | 锁定相机、Boss HP/架势 HUD、命中特效、音效、Hit Stop、输入与弹刀窗口调优 | 一场 Boss 战可以从进入战斗完整运行到结算 |

### Boss AI 方向

后期计划让 AI 行为树采用与 Lua 动画蓝图相近的开发方式：使用 Lua 声明行为树结构、条件、任务和数据配置，再生成或映射为 UE 原生 Behavior Tree / Blackboard，由引擎运行时执行。

这样可以让 Boss 行为保持易读、易修改和可版本管理，同时保留 UE 原生行为树的运行效率。具体 Lua DSL、编译流程和编辑器工具将在正式实现 Boss AI 时单独设计。

### 第一场 Boss 战验收目标

第一场 Boss 战以完整性优先，不追求一次覆盖所有只狼机制：

- 玩家能够完成轻重攻击、防御、弹刀、受击、架势崩坏和忍杀。
- Boss 能主动接敌、管理距离、执行至少三种普通攻击和一种危字攻击。
- Boss 能防御或弹刀玩家攻击，并正确进入受击、硬直和架势崩坏状态。
- Boss 至少具有两个阶段，阶段切换会替换或调整攻击模式。
- 锁定、相机、HP/架势 HUD、死亡与退出战斗流程完整。
- 所有动作都能被中断和清理，不遗留碰撞盒、Montage、输入缓存或 AI 任务。

详细系统依赖见 [系统索引](Docs/gdd/systems-index.md)，攻防实现基线见 [角色攻击防御系统技术设计](Docs/design/sekiro-attack-defense-system.md)，Boss 决策模型见 [Boss AI 系统设计](Docs/gdd/boss-ai-system.md)。

## 开发约定

- `Source/Sekiro/` 的项目类型使用 `SK` 前缀；`Plugins/` 的公共类型使用 `Sekiro` 全称前缀。
- C++ 插件只提供通用接口，不硬编码项目资产路径；具体玩法工作流由脚本层编排。
- Lua 动画蓝图在编辑器阶段编译，运行时不得逐帧重新加载或编译声明模块。
- 新增或修改 `Content/Script/**/*.lua` 时遵循 [Lua 代码规范](Docs/lua-code-style.md)，并补齐必要的中文注释。
- 本地生成目录、引擎构建产物、原始提取数据和第三方工具不应提交到仓库。

## 验证

- C++ 或插件修改：使用 Unreal Build Tool 编译 `SekiroEditor Win64 Development`。
- Lua 文档注释：运行 `python Script/check_lua_function_docs.py`。
- 资产与动画生成：使用对应专题文档中的验证脚本，检查生成结果后再进行 PIE 场景测试。

## 声明

本项目用于技术研究与学习，与 FromSoftware 或 Activision 无隶属关系。《只狼：影逝二度》及其相关商标和原始资产归各自权利人所有。
