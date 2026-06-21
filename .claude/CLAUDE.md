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

## 工作流

```
/plan <需求>        → 方案设计 + 任务拆分 → Docs/plan/<需求>.md
/delegate <复杂问题>  → 开启 subagent 净化上下文 → 处理 → 返回结果
/review <范围>           → 审查报告 + 修复
```

所有对话、建议、文档使用简体中文。
