# Claude Code Game Studios -- 游戏工作室代理架构

通过 48 个协同的 Claude Code 子代理管理独立游戏开发。
每个代理负责特定领域，强制关注点分离和质量控制。

## 技术栈

- **引擎**：Unreal Engine 5.2
- **语言**：C++（主要），Blueprint + Lua（脚本层）
- **版本控制**：Git，基于主干开发
- **构建系统**：Unreal Build Tool (UBT)
- **资产管线**：Unreal Content Pipeline

> **注意**：本项目使用 Unreal Engine 5.2，配备专属的 UE 专家代理
> （ue-blueprint-specialist、ue-gas-specialist、ue-replication-specialist、ue-umg-specialist 等）。

## 项目结构

@.claude/docs/directory-structure.md

## 引擎版本参考

@Docs/engine-reference/unreal/VERSION.md

## UE 技能库

@.claude/rules/ue-skills.md

技能文件位于 `Docs/engine-reference/unreal/skills/`，覆盖 UE 各子系统。
所有 UE 代理在给出建议前必须查阅对应技能文件。
发现技能内容有误或不足时，应立即勘误/扩充，并确保 SKILL.md 不超过 500 行。

## 技术偏好

@.claude/docs/technical-preferences.md

## 协调规则

@.claude/docs/coordination-rules.md

## 协作协议

**用户驱动的协作，而非自主执行。**
每个任务遵循：**提问 -> 选项 -> 决定 -> 草稿 -> 审批**

- 代理在使用 Write/Edit 工具之前必须询问"可以将此内容写入 [文件路径] 吗？"
- 代理必须在请求审批之前展示草稿或摘要
- 多文件变更需要明确批准整个变更集
- 没有用户指示，不得进行任何提交

完整协议和示例请参见 `Docs/COLLABORATIVE-DESIGN-PRINCIPLE.md`。

> **第一次使用？** 如果项目没有配置引擎且没有游戏概念，
> 运行 `/start` 开始引导式入门流程。

## 编码标准

@.claude/docs/coding-standards.md

## 上下文管理

@.claude/docs/context-management.md
所有对话、解释、建议必须使用**简体中文**。

## 项目记忆（跨机器同步）

@.claude/memory/MEMORY.md

记忆文件位于 `.claude/memory/`，在仓库内版本控制，可在不同机器间同步。
内容包括：项目概览、结构定义、用户偏好、当前工作进度。

## 当前实施计划

@Docs/implementation-plan.md

SekiroImport C++ 插件重构计划（JSON→UE资产直接导入，对齐Blender管线）。
进度：Phase 0+1+2 完成，Phase 3/4/5 待实施。
