```markdown
---
name: team-narrative
description: "编排叙事团队：协调 narrative-director、writer、world-builder 和 level-designer，打造连贯的故事内容、世界传说（Lore）和叙事驱动的关卡设计。"
argument-hint: "[叙事内容描述]"
user-invocable: true
allowed-tools: Read, Glob, Grep, Write, Edit, Task, AskUserQuestion, TodoWrite
---
当调用此技能时，通过结构化管线（Pipeline）编排叙事团队。

**决策节点：** 在每个阶段转换时，使用 `AskUserQuestion` 将子代理的提案以可选项的形式呈现给用户。在对话中写出代理的完整分析，然后用简洁的标签记录决策。用户必须批准后才能进入下一阶段。

## 团队组成
- **narrative-director**（叙事总监）— 故事弧线（Story Arc）、角色设计、对话策略、叙事愿景
- **writer**（编剧）— 对话撰写、传说条目、物品描述、游戏内文本
- **world-builder**（世界构建师）— 世界规则、阵营设计、历史、地理、环境叙事（Environmental Storytelling）
- **level-designer**（关卡设计师）— 服务于叙事的关卡布局、节奏控制、环境叙事节拍

## 委派方式

使用 Task 工具将每位团队成员生成为子代理：
- `subagent_type: narrative-director` — 故事弧线、角色设计、叙事愿景
- `subagent_type: writer` — 对话撰写、传说条目、游戏内文本
- `subagent_type: world-builder` — 世界规则、阵营设计、历史、地理
- `subagent_type: level-designer` — 服务于叙事的关卡布局、节奏控制

始终在每个代理的提示中提供完整上下文（叙事简报、传说依赖关系、角色档案）。在管线允许的情况下并行启动独立代理（例如第 2 阶段的代理可同时运行）。

## 管线（Pipeline）

### 第 1 阶段：叙事方向
委派给 **narrative-director**：
- 定义此内容的叙事目的：它服务于哪个故事节拍（Story Beat）？
- 识别涉及的角色、其动机，以及如何融入整体故事弧线
- 设定情感基调和节奏目标
- 指定传说依赖项或此内容引入的新传说
- 输出：包含故事需求的叙事简报（Narrative Brief）

### 第 2 阶段：世界基础（并行）
并行委派：
- **world-builder**：创建或更新与此内容相关的阵营、地点和历史传说条目。与现有传说交叉比对以检查矛盾。为新条目设定正典级别（Canon Level）。
- **writer**：使用声音档案（Voice Profile）起草角色对话。确保所有对话行不超过 120 个字符，使用命名占位符表示变量，并做好本地化准备。

### 第 3 阶段：关卡叙事整合
委派给 **level-designer**：
- 审查叙事简报和传说基础
- 设计关卡中的环境叙事元素
- 放置叙事触发器、对话区域和发现点
- 确保节奏同时服务于玩法和故事

### 第 4 阶段：审查与一致性
委派给 **narrative-director**：
- 对照角色声音档案审查所有对话
- 验证新旧传说条目之间的一致性
- 确认叙事节奏与关卡设计对齐
- 检查所有谜团是否都记录了"真实答案"

### 第 5 阶段：打磨
- 编剧审查所有文本的本地化就绪状态
- 验证没有对话行超出对话框约束
- 确认所有文本使用字符串键（本地化管线就绪）
- 世界构建师为所有新传说最终确定正典级别

## 输出
一份总结报告，涵盖：叙事简报状态、已创建/更新的传说条目、已编写的对话行、关卡叙事整合点、一致性审查结果，以及任何未解决的矛盾。
```
