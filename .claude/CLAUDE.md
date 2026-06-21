# Sekiro → UE5.2

只狼游戏系统迁移到 Unreal Engine 5.2。

## 技术栈

- **引擎**：Unreal Engine 5.2
- **语言**：C++（游戏核心 + 插件接口），Python（管线脚本），Lua（脚本层，UnLua）
- **构建**：Unreal Build Tool (UBT)

## 项目结构

@.claude/docs/directory-structure.md

## 编码规范

@.claude/rules/code-style.md
@.claude/rules/cpp-workflow.md
@.claude/rules/aibridge-workflow.md
@.claude/rules/bug-fix-workflow.md

**核心架构原则**：C++ 只提供通用接口（UFUNCTION），脚本层负责编排具体工作流。
**需要修改 `.h` / `.cpp` 时，必须先读取 cpp-workflow.md 然后按规则执行。**
**操作 UE 编辑器时，必须先读取 aibridge-workflow.md 然后按规则执行。**
**分析未知的 Bug/问题/错误时，必须先读取 bug-fix-workflow.md 然后按规则执行。**

## 技能（/ 命令）

| 技能 | 用途 |
|------|------|
| `/aibridge` | 通过 TCP JSON-RPC 操控 UE5 编辑器（查询/编译/蓝图/资产/输入） |
| `/sekiro-asset-import` | 只狼资产导入管线：解包→FLVER→JSON→UE 完整流程 |
| `/plan` | 方案设计 + 任务拆分 → `Docs/plan/`，支持递归、追踪、修改 |
| `/breakdown` | 需求拆分 → `Docs/breakdown/`，支持递归、追踪、修改 |
| `/delegate` | 净化上下文，委托 subagent 处理复杂多步骤问题 |
| `/reasoning` | 多专家并行分析复杂问题，产出方案文档 |
| `/tech-design` | 技术方案 + Agent 派发 → `Docs/tech-designs/`，逐子任务推进 |
| `/review` | 代码审查，风格问题自动修复，违规派发对应 Agent |

## Agent

| Agent | 职责 | 代码位置 |
|-------|------|---------|
| plugin-programmer | C++ 插件接口（零硬编码，只引用引擎） | `Plugins/` |
| gameplay-programmer | 游戏机制（战斗/移动/角色） | `Source/Sekiro/` |
| script-agent | 管线脚本（编译/导入/编排） | `Script/` |
| review-agent | 代码审查（被 `/review` 调用） | — |
| function-validator | 功能验证（编译/AIBridge/脚本），不写代码 | — |

## 工作流

```
/plan <需求>               → Docs/plan/<需求>.md
/breakdown <需求>          → Docs/breakdown/<需求>.md
/tech-design <需求> <任务>  → Docs/tech-designs/<需求>-<任务>.md
  → Agent 执行
/review <范围>              → 审查报告 + 修复
```

所有对话、建议、文档使用**简体中文**。

## 编码规则

- **文件编码**：所有 Python 脚本（`.py`）必须使用 **UTF-8** 编码，禁止 GBK/BIG5 等。如发现文件被存为 GBK，需先转码后再修改。
- **缩进**：统一使用 **4 空格**缩进，禁止混用 tab 和空格。

## 临时脚本规则

所有临时生成的脚本文件（如检查脚本、一次性验证脚本等），必须写入 `Script/temp/`，不得留在技能目录或其他位置。
