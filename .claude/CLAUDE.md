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

**核心架构原则**：C++ 只提供通用接口（UFUNCTION），脚本层负责编排具体工作流。
**需要修改 `.h` / `.cpp` 时，必须先读取 cpp-workflow.md 然后按规则执行。**

## 技能（/ 命令）

| 技能 | 用途 |
|------|------|
| `/breakdown` | 需求拆分 → `.claude/docs/breakdown/`，支持递归、追踪、修改 |
| `/tech-design` | 技术方案 + Agent 派发 → `.claude/docs/tech-designs/`，逐子任务推进 |
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
/breakdown <需求>          → .claude/docs/breakdown/<需求>.md
/tech-design <需求> <任务>  → .claude/docs/tech-designs/<需求>-<任务>.md
  → Agent 执行
/review <范围>              → 审查报告 + 修复
```

所有对话、建议、文档使用**简体中文**。

## 临时脚本规则

所有临时生成的脚本文件（如检查脚本、一次性验证脚本等），必须写入 `Script/temp/`，不得留在技能目录或其他位置。
