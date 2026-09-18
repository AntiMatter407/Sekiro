# 项目目录结构

本文说明仓库中主要目录的职责。下方目录树只展示 `git ls-files` 可见、会随主仓库同步的内容；UE 自动生成目录、本地缓存、未跟踪文件和 Gitlink 内部内容均不展示。

```text
Sekiro/
├── Source/Sekiro/                  C++ 游戏运行时（SK 前缀）
│   ├── Animation/                  动画实例、Root Motion、Foot IK 与动画接口
│   ├── Camera/                     相机系统
│   ├── Character/                  角色基础
│   ├── Combat/                     战斗、攻击与防御
│   ├── Core/                       项目核心类型
│   ├── Input/                      Enhanced Input 与输入语义
│   ├── Movement/                   移动系统
│   ├── UI/                         UI 运行时
│   └── Weapon/                     武器系统
├── Content/
│   ├── Script/                     Lua / UnLua 脚本
│   │   ├── Animation/              Lua 动画蓝图、编译器与动画层
│   │   ├── Gameplay/               玩法脚本
│   │   └── Debug/                  Lua 调试辅助
│   ├── Characters/                 角色资产
│   ├── Gameplay/                   游戏性资产
│   ├── Input/                      输入资产
│   ├── LevelPrototyping/           关卡原型资产
│   ├── StarterContent/             UE Starter Content
│   ├── ThirdPerson/                Third Person 模板资产
│   └── Weapons/                    武器资产
├── Plugins/
│   ├── LuaEditorExtensions/        通用 Lua 编辑器扩展：动画蓝图、行为树与 Gameplay 模块
│   ├── SekiroAssetManager/         Sekiro 资产导入编辑器插件
│   ├── SekiroAIBridge/             UE 编辑器 TCP JSON-RPC 桥接
│   ├── UnLua/                      Lua 运行时集成
│   └── UnLuaExtensions/            LuaSocket、Protobuf、RapidJSON 扩展
├── Script/
│   ├── sekiro_asset_manager/       资产提取、转换、导入与验证管线
│   ├── aibridge/                   AIBridge 客户端与辅助脚本
│   └── *.py / *.ps1                专项迁移、修复和验证脚本
├── Docs/
│   ├── gdd/                        游戏设计与系统索引
│   ├── design/                     技术设计
│   ├── plan/                       实施计划与进度
│   ├── engine-reference/           UE5.2 引擎参考
│   ├── examples/                   示例
│   └── *.md                        系统、管线与使用指南
├── .codex/                         项目技能、规则与本机设置
├── Config/                         UE 项目配置
├── Sekiro.uproject                 UE 项目入口
└── README.md                       仓库入口与快速开始
```

## 代码边界

| 位置 | 职责 | 命名约定 |
|---|---|---|
| `Source/Sekiro/` | 只狼项目专用的游戏运行时 | `SK` 前缀 |
| `Plugins/` | 可复用的引擎、编辑器与脚本扩展 | 通用插件使用能力语义命名；项目专用插件才使用 `Sekiro` 前缀 |
| `Content/Script/` | Lua 动画声明和玩法编排 | 遵循项目 Lua 规范 |
| `Script/` | 离线管线、批处理、诊断与验证 | UTF-8 Python / PowerShell |

C++ 负责稳定、通用的底层能力，Lua 负责项目工作流与配置。插件不得引用 `Source/Sekiro/`，也不得硬编码本机路径或具体项目资产。

## 生成目录与本地数据

以下目录通常由 UE、管线或本地工具生成，不应作为手写源码维护：

- `Binaries/`、`Intermediate/`、`DerivedDataCache/`、`Saved/`、`TempBuild/`
- 各插件下的 `Binaries/`、`Intermediate/`
- `Extracted/` 中的原始游戏数据
- `Tools/` 中的本地第三方可执行文件
- `Script/temp/` 中的一次性诊断脚本

是否提交某个生成结果以 `.gitignore` 和具体管线文档为准；不要通过移动或清理这些目录来修复源码引用。

## 未完整同步的仓库引用

以下路径没有显示在同步目录树中：

- `Output/`：主仓库只记录 Gitlink，内部文件不会随主仓库同步。
- `Script/sekiro_asset_manager/ext_tools/SoulsAssetPipeline/`：主仓库只记录 Gitlink；多个 `.csproj` 直接依赖其源码。

仓库当前没有 `.gitmodules`。因此普通 `git clone --recurse-submodules` 无法还原这两个目录；在补齐可访问的远端地址和已推送的固定提交前，不能把它们视为可复现依赖。

## 文档入口

- 游戏系统：[gdd/systems-index.md](gdd/systems-index.md)
- Lua 动画蓝图：[lua-anim-blueprint-authoring-guide.md](lua-anim-blueprint-authoring-guide.md)
- Lua 动画运行时：[lua-animgraph-runtime-architecture.md](lua-animgraph-runtime-architecture.md)
- 资产导入：[sekiro-asset-import.md](sekiro-asset-import.md)
- AIBridge：[aibridge-reference.md](aibridge-reference.md)
- Lua 代码规范：[lua-code-style.md](lua-code-style.md)
