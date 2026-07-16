# Sekiro → UE5.2

只狼游戏系统迁移到 Unreal Engine 5.2。

## 技术栈

- **引擎**: Unreal Engine 5.2（路径：`$env:UE_ENGINE_DIR`，配置于 `.codex/settings.local.json`）
- **语言**: C++（游戏核心 + 插件接口），Python（管线脚本），Lua（脚本层，UnLua）
- **构建**: Unreal Build Tool (UBT)

## 项目结构

@Docs/directory-structure.md

```
Source/Sekiro/          # 游戏代码（C++）— 使用 SK 前缀
Plugins/                # 插件（C++）— 使用 Sekiro 全称前缀
Script/                 # Python 管线脚本
Content/                # UE 资源
Tools/                  # 外部工具（Yabber, FlverToFbx, texconv）
.codex/skills/          # 项目技能定义
.codex/rules/           # 代码规则
```

## 技能（/ 命令）

| 技能 | 用途 |
|------|------|
| `/aibridge` | 通过 TCP JSON-RPC 操控 UE5 编辑器（查询/编译/蓝图/资产/输入） |
| `/sekiro-asset-import` | 只狼资产导入管线：解包→FLVER→JSON→UE 完整流程 |
| `/plan` | 方案设计 + 详细设计。分析需求，输出 `Docs/plan/<slug>.md`（进度+任务树）和 `Docs/design/<slug>.md`（单文档方案，子任务作为章节），追踪进度 |
| `/delegate` | 将复杂问题委托给新 subagent，净化上下文专注处理 |
| `/review` | 代码审查，风格问题自动修复，违规派发对应 Agent |

## Agent 边界（强制）

| Agent | 职责 | 代码位置 | 禁止 |
|-------|------|---------|------|
| gameplay-programmer | 游戏机制（战斗/移动/角色） | `Source/Sekiro/` | 修改 Plugins/、构建文件 |
| plugin-programmer | C++ 插件接口（零硬编码，只引用引擎） | `Plugins/` | 修改 Source/Sekiro/、硬编码路径 |
| review-agent | 代码审查（被 /review 调用） | — | — |
| expert-agent | 多角度分析（被 /reasoning 调用） | — | — |

## 编码规范

@.codex/rules/code-style.md
@Docs/lua-code-style.md
@.codex/rules/cpp-workflow.md
@.codex/rules/aibridge-workflow.md
@.codex/rules/bug-fix-workflow.md

**核心架构原则**：C++ 只提供通用接口（UFUNCTION），脚本层负责编排具体工作流。
**新增或修改 `Content/Script/**/*.lua` 时，必须先读取并遵守 `Docs/lua-code-style.md`，并补齐必要中文注释。**
**需要修改 `.h` / `.cpp` 时，必须先读取 cpp-workflow.md 然后按规则执行。**
**新增 C++ 函数时，完整的职责、参数、返回值和线程约束注释以 `.cpp` 实现处为准；`.h` 只保留必要的接口摘要、UHT 提示，以及无独立实现的模板/内联/纯虚函数文档。**
**操作 UE 编辑器时，必须先读取 aibridge-workflow.md 然后按规则执行。**
**分析未知的 Bug/问题/错误时，必须先读取 bug-fix-workflow.md 然后按规则执行。**

关键约定：
- `Source/Sekiro/` 使用 `SK` 缩写前缀（ASKCharacter、USKAnimInstance）
- `Plugins/` 使用 `Sekiro` 全称前缀（USekiroImportLibrary）
- UPROPERTY 宏独占一行，变量下一行，同行中文注释
- 所有 Python 脚本使用 UTF-8 编码
- 所有项目 Lua 脚本使用 UTF-8 编码，4 空格缩进，详细中文注释
- 统一使用 4 空格缩进
- 禁止单字母下划线前缀
- 临时脚本写入 `Script/temp/`

## 工作流

```
/plan <需求>        → 方案设计 + 任务拆分 → Docs/plan/<需求>.md
/delegate <复杂问题>  → 开启 subagent 净化上下文 → 处理 → 返回结果
/review <范围>           → 审查报告 + 修复
```

## 编码要求

所有 `.h` / `.cpp` 使用 **UTF-8 with BOM**。禁止用 Bash/PowerShell 的 `Set-Content` 等命令写入源码文件——只能用 Write/Edit 工具，否则中文注释会损坏。

所有对话、建议、文档使用简体中文。
