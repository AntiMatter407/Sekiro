# Sekiro → UE5.2

Sekiro游戏系统移植到Unreal Engine 5.2的项目。

## 技术栈

- **引擎**: Unreal Engine 5.2（路径：`$env:UE_ENGINE_DIR`，配置于 `.codex/settings.local.json`）
- **语言**: C++（游戏核心 + 插件接口），Python（管线脚本），Lua（脚本层，UnLua）
- **构建**: Unreal Build Tool (UBT)

## 项目结构

```
Source/Sekiro/          # 游戏代码（C++）— 使用 SK 前缀
Plugins/                # 插件（C++）— 使用 Sekiro 全称前缀
Script/                 # Python 管线脚本
Content/                # UE 资源
Tools/                  # 外部工具（Yabber, FlverToFbx, texconv）
.codex/skills/          # 项目技能定义
.codex/rules/           # 代码规则
```

核心架构原则：**C++ 只提供通用接口（UFUNCTION），脚本层负责编排具体工作流。**

## 技能（/ 命令）

| 技能 | 用途 |
|------|------|
| `/aibridge` | 通过 TCP JSON-RPC 操控 UE5 编辑器（查询/编译/蓝图/资源/输入） |
| `/sekiro-asset-import` | Sekiro 资产导入管线：解包→FLVER→JSON→UE 完整流程 |
| `/plan` | 方案设计 + 任务拆分 → `Docs/plan/`，追踪进度 |
| `/delegate` | 将复杂问题委托给新 subagent，净化上下文专注处理 |
| `/review` | 代码审查，风格问题自动修复，违规派发对应 Agent |

## Agent 边界（强制）

| Agent | 职责 | 代码位置 | 禁止 |
|-------|------|---------|------|
| gameplay-programmer | 游戏机制（战斗/移动/角色） | `Source/Sekiro/` | 修改 Plugins/、构建文件 |
| plugin-programmer | C++ 插件接口（零硬编码，只引用引擎） | `Plugins/` | 修改 Source/Sekiro/、硬编码路径 |
| review-agent | 代码审查（被 /review 调用） | — | — |
| expert-agent | 多角度分析（被 /reasoning 调用） | — | — |

## 编码规则摘要

@.codex/rules/code-style.md
@.codex/rules/cpp-workflow.md
@.codex/rules/aibridge-workflow.md
@.codex/rules/bug-fix-workflow.md

关键约定：
- `Source/Sekiro/` 使用 `SK` 缩写前缀（ASKCharacter、USKAnimInstance）
- `Plugins/` 使用 `Sekiro` 全称前缀（USekiroImportLibrary）
- UPROPERTY 宏独占一行，变量下一行，同行中文注释
- 所有 Python 脚本使用 UTF-8 编码
- 统一使用 4 空格缩进
- 禁止单字母下划线前缀
- 临时脚本写入 `Script/temp/`

## 工作流

```
/plan <需求>        → 方案设计 + 任务拆分 → Docs/plan/<需求>.md
/delegate <复杂问题>  → 开启 subagent 净化上下文 → 处理 → 返回结果
/review <范围>           → 审查报告 + 修复
```

所有对话、建议、文档使用简体中文。