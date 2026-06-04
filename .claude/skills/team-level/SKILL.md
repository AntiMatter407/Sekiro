---
name: team-level
description: "编排关卡设计团队：level-designer + narrative-director + world-builder + art-director + systems-designer + qa-tester，完成完整的区域/关卡创建。"
argument-hint: "[关卡名称或要设计的区域]"
user-invocable: true
allowed-tools: Read, Glob, Grep, Write, Edit, Bash, Task, AskUserQuestion, TodoWrite
---

当此技能被调用时：

**决策节点：** 在每个步骤转换时，使用 `AskUserQuestion` 将子代理的方案以可选项的形式呈现给用户。在对话中写出代理的完整分析，然后以简洁的标签记录决策。用户必须批准后才能进入下一步。

1. **读取参数**以确定目标关卡或区域（例如 `新手教程`、`森林地牢`、`枢纽城镇`、`最终 Boss 竞技场`）。

2. **收集上下文**：
   - 读取位于 `Docs/gdd/game-concept.md` 的游戏概念文档
   - 读取位于 `Docs/gdd/game-pillars.md` 的游戏支柱文档
   - 读取 `Docs/levels/` 中的现有关卡文档
   - 读取 `Docs/narrative/` 中的相关叙事文档
   - 读取该区域所属地区/阵营的世界观设定文档

## 如何委派

使用 Task 工具将每位团队成员生成为子代理：
- `subagent_type: narrative-director` — 叙事目的、角色、情感弧线
- `subagent_type: world-builder` — 背景设定上下文、环境叙事、世界规则
- `subagent_type: level-designer` — 空间布局、节奏、遭遇战、导航
- `subagent_type: systems-designer` — 敌人组合、战利品表、难度平衡
- `subagent_type: art-director` — 视觉主题、调色板、光照、资产需求
- `subagent_type: qa-tester` — 测试用例、边界测试、试玩检查清单

始终在每个代理的提示中提供完整上下文（游戏概念、支柱、现有关卡文档、叙事文档）。

3. **按顺序编排关卡设计团队**：

### 步骤 1：叙事上下文（narrative-director + world-builder）
生成 `narrative-director` 代理以：
- 定义该区域的叙事目的（这里发生哪些故事节拍？）
- 识别关键角色、对话触发点和背景设定元素
- 指定情感弧线（玩家进入时、探索中、离开时应有什么感受？）

生成 `world-builder` 代理以：
- 提供该区域的背景设定上下文（历史、阵营势力、生态）
- 定义环境叙事的机会
- 指定影响该区域玩法的世界规则

### 步骤 2：布局与遭遇战设计（level-designer）
生成 `level-designer` 代理以：
- 设计空间布局（关键路径、可选路径、隐藏区域）
- 定义节奏曲线（紧张高峰、休息区域、探索区域）
- 放置具有难度递进的遭遇战
- 设计环境谜题或导航挑战
- 定义兴趣点和地标以辅助寻路
- 指定入口/出口点及与相邻区域的连接

### 步骤 3：系统集成（systems-designer）
生成 `systems-designer` 代理以：
- 指定敌人组合和遭遇战公式
- 定义战利品表和奖励放置
- 根据玩家预期等级/装备平衡难度
- 设计区域专属机制或环境危害
- 指定资源分布（生命恢复道具、存档点、商店）

### 步骤 4：视觉方向（art-director）
生成 `art-director` 代理以：
- 定义该区域的视觉主题和调色板
- 指定光照氛围和时间设置
- 列出所需美术资产（环境道具、专属资产）
- 定义视觉地标和视线
- 指定特殊视觉特效需求（天气、粒子、雾气）

### 步骤 5：QA 规划（qa-tester）
生成 `qa-tester` 代理以：
- 编写关键路径的测试用例
- 识别边界情况和极端情况（顺序越界、软锁定）
- 创建该区域的试玩检查清单
- 定义关卡完成的验收标准

4. **编制关卡设计文档**，将所有团队输出整合为关卡设计模板格式。

5. **保存至** `Docs/levels/[关卡名称].md`。

6. **输出摘要**，包含：区域概述、遭遇战数量、预估资产列表、叙事节拍，以及跨团队依赖关系或待解决问题。
