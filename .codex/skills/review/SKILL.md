---
name: review
description: "代码审查技能。调用 Review Agent 检查代码风格和需求合规性，自动修复风格问题，其他问题派发对应 Agent。"
---

# /review — 代码审查

## 触发方式

| 命令 | 作用 |
|------|------|
| `/review <文件/目录>` | 审查指定文件或目录 |
| `/review <需求名称>` | 审查该需求涉及的所有代码 |
| `/review --staged` | 审查 git 暂存区的变更 |

审查仅在被主动调用时执行，不会自动触发。

## 工作流

```
/review <范围>
      │
      ▼
  1. 收集审查范围内的代码文件
  2. 调用 Review Agent，传入文件列表 + 相关需求文档
      │
      ▼
  Review Agent 输出审查报告：
      │
      ├── 风格问题 → Agent 直接修复
      └── 需求违规 → 列出来，建议对应 Agent
                         │
                         ▼ 用户确认
                    调用对应 Agent 修复
```

## 审查规则来源

Review Agent 对比以下规则进行检查：

| 规则来源 | 内容 |
|---------|------|
| `.codex/rules/code-style.md` | 命名前缀、UPROPERTY 格式、注释规范等 |
| `.codex/agents/plugin-programmer.md` | 插件零硬编码、依赖方向等硬性约束 |
| `.codex/agents/gameplay-programmer.md` | 游戏代码编码规范 |
| `Docs/design/<需求>.md` | 技术方案中的 API 设计、涉及文件等 |
| `Docs/plan/<需求>.md` | 需求任务树的完成标准 |

## 输出格式

```
# Review 报告：<范围>

## 风格问题（已自动修复）
- [文件:行号] 问题描述 → ✅ 已修复
- [文件:行号] 问题描述 → ✅ 已修复

## 需求违规（需 Agent 修复）
- [文件:行号] 违规描述
  → 建议：plugin-programmer 修复（硬编码路径）
  → 建议：gameplay-programmer 重构（违反依赖方向）

## 总结
- 风格问题：N 个（已修复）
- 违规问题：M 个（待确认）
```
