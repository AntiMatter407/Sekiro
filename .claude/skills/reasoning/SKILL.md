---
name: reasoning
description: "推理分析技能。将复杂问题派发给多个专家 Agent 并行/串行分析，支持多方案对比、评审迭代。"
---
# /reasoning — 推理分析

## 触发方式

| 命令 | 作用 |
|------|------|
| `/reasoning <问题描述>` | 对复杂问题启动推理分析流程 |

## 适用场景

1. **复杂方案设计** — tech-design 遇到难以决策的架构问题
2. **多方案对比** — 需要从多个角度对比不同方案
3. **方案评审迭代** — 需要一个专家评审另一个专家的方案
4. **代码审计** — 安全/性能/架构多维度分析

## 工作流

```
/reasoning <复杂问题>
      │
      ▼
  1. 理解问题 → 决定推理模式
      │
      ├── 多方案模式：并行派发 N 个 expert-agent，各用不同模型/视角
      │      → 收集方案 → 评审 → 选优/融合
      │
      └── 评审迭代模式：派发一个 expert-agent 出方案
             → 派发另一个 expert-agent 评审
             → 让第一个 agent 根据意见修改
             → 可多轮迭代
      │
  2. 展示推理过程/结果
  3. 产出最终方案文档 → `.claude/docs/tech-designs/` 或对话中输出
```

## 多方案模式

并行派发多个 expert-agent，每个使用不同模型，从不同视角出方案：

```
Agent 1: expert-agent, model: opus          (deepseek-chat, 综合视角)
Agent 2: expert-agent, model: custom-2      (Qwen Max, 中文强项)
Agent 3: expert-agent, model: fable         (Qwen3-Coder-Plus, 代码视角)

→ 收集 3 个方案
→ 选最优 / 融合
→ 可选：再派一个 expert-agent 评审选中的方案
```

调用方式（在 Workflow 中）：

```js
const agents = await Promise.all([
  agent(`作为专家，设计：${problem}`, {label: '专家1-综合', agentType: 'expert-agent', model: 'opus'}),
  agent(`作为专家，设计：${problem}`, {label: '专家2-Qwen', agentType: 'expert-agent', model: 'custom-2'}),
  agent(`作为专家，设计：${problem}`, {label: '专家3-Coder', agentType: 'expert-agent', model: 'fable'}),
])
```

## 评审迭代模式

串行流程：方案 → 评审 → 修改 → 再评审（可选多次）

```
Agent 1: expert-agent, model: opus     → 出方案
Agent 2: expert-agent, model: custom-2  → 评审方案
Agent 1: expert-agent, model: opus     → 根据评审意见修改方案
（可选）Agent 2 → 再评审 → 方案定稿
```

调用方式（在 Workflow 中）：

```js
const plan = await agent(`设计方案：${problem}`, {label: '出方案', agentType: 'expert-agent', model: 'opus'})

const review = await agent(`评审以下方案：\n${plan}`, {label: '评审方案', agentType: 'expert-agent', model: 'custom-2'})

const final = await agent(`根据评审意见修改方案。\n原方案：${plan}\n\n评审意见：${review}`, {label: '修改方案', agentType: 'expert-agent', model: 'opus'})
```

## 规则

- **方案文档产出**：最终方案写入 `.claude/docs/tech-designs/`（格式与 tech-design 一致）
- **告知使用模型**：向用户展示每个 Agent 使用了什么模型
- **迭代上限**：最多 3 轮评审迭代，防止无限循环
- **成本透明**：告知用户本次调用使用了几个 Agent、各用什么模型
