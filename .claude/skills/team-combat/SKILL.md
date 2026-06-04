---
name: team-combat
description: "编排战斗团队：协调 game-designer、gameplay-programmer、ai-programmer、technical-artist、sound-designer 和 qa-tester，端到端地设计、实现并验证战斗功能。"
argument-hint: "[战斗功能描述]"
user-invocable: true
allowed-tools: Read, Glob, Grep, Write, Edit, Bash, Task, AskUserQuestion, TodoWrite
---
当此技能被调用时，通过结构化流水线编排战斗团队。

**决策点：** 在每个阶段转换时，使用 `AskUserQuestion` 将子代理的方案作为可选项呈现给用户。在对话中展示代理的完整分析，然后用简洁的标签记录决策。用户批准后方可进入下一阶段。

## 团队组成
- **game-designer（游戏设计师）** — 设计机制，定义公式和边缘情况（Edge Case）
- **gameplay-programmer（玩法程序员）** — 实现核心玩法代码
- **ai-programmer（AI 程序员）** — 实现与该功能相关的 NPC/敌人 AI 行为
- **technical-artist（技术美术）** — 创建 VFX（视觉效果）、着色器（Shader）效果和视觉反馈
- **sound-designer（音效设计师）** — 定义音频事件、打击音效和环境战斗音频
- **qa-tester（QA 测试员）** — 编写测试用例并验证实现

## 如何委派

使用 Task 工具将每位团队成员生成为子代理：
- `subagent_type: game-designer` — 设计机制，定义公式和边缘情况
- `subagent_type: gameplay-programmer` — 实现核心玩法代码
- `subagent_type: ai-programmer` — 实现 NPC/敌人 AI 行为
- `subagent_type: technical-artist` — 创建 VFX、着色器效果、视觉反馈
- `subagent_type: sound-designer` — 定义音频事件、打击音效、环境音频
- `subagent_type: qa-tester` — 编写测试用例并验证实现

始终在每个代理的提示中提供完整上下文（设计文档路径、相关代码文件、约束条件）。在流水线允许的情况下并行启动独立代理（例如，阶段 3 的代理可以同时运行）。

## 流水线

### 阶段 1：设计
委派给 **game-designer**：
- 在 `Docs/gdd/` 中创建或更新设计文档，涵盖：机制概述、玩家幻想、详细规则、含变量定义的公式、边缘情况、依赖项、含安全范围的调优旋钮（Tuning Knob）以及验收标准（Acceptance Criteria）
- 输出：完成的设计文档

### 阶段 2：架构设计
委派给 **gameplay-programmer**（如涉及 AI，同时委派 **ai-programmer**）：
- 审查设计文档
- 设计代码架构：类结构、接口、数据流
- 识别与现有系统的集成点
- 输出：架构草图，含文件列表和接口定义

### 阶段 3：实现（尽可能并行）
并行委派：
- **gameplay-programmer**：实现核心战斗机制代码
- **ai-programmer**：实现 AI 行为（如果功能涉及 NPC 反应）
- **technical-artist**：创建 VFX 和着色器效果
- **sound-designer**：定义音频事件列表和混音说明

### 阶段 4：集成
- 连接玩法代码、AI、VFX 和音频
- 确保所有调优旋钮已暴露且数据驱动
- 验证功能与现有战斗系统的兼容性

### 阶段 5：验证
委派给 **qa-tester**：
- 根据验收标准编写测试用例
- 测试设计文档中记录的所有边缘情况
- 验证性能影响在预算范围内
- 为发现的问题提交缺陷报告（Bug Report）

### 阶段 6：签收
- 收集所有团队成员的结果
- 报告功能状态：完成 / 需要修改 / 阻塞
- 列出所有未解决问题及其负责人

## 输出
一份总结报告，涵盖：设计完成状态、各团队成员的实现状态、测试结果以及任何未解决的问题。
