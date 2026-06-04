---
name: team-audio
description: "编排音频团队：audio-director + sound-designer + technical-artist + gameplay-programmer，覆盖从音频方向制定到落地的完整音频管线。"
argument-hint: "[需要设计音频的功能或区域]"
user-invocable: true
allowed-tools: Read, Glob, Grep, Write, Edit, Bash, Task, AskUserQuestion, TodoWrite
---

当此技能被调用时，通过结构化管线编排音频团队。

**决策点**：在每个步骤转换时，使用 `AskUserQuestion` 将子代理的提案以可选项的形式呈现给用户。在对话中完整展示代理的分析内容，然后以简洁标签记录决策。用户必须批准后才能进入下一步。

1. **读取参数**，获取目标功能或区域（例如 `combat`、`main menu`、`forest biome`、`boss encounter`）。

2. **收集上下文**：
   - 读取 `Docs/gdd/` 中与该功能相关的设计文档
   - 读取音频圣经 `Docs/gdd/sound-bible.md`（如存在）
   - 读取 `assets/audio/` 中现有的音频资产列表
   - 读取该区域已有的音效设计文档

## 如何委派

使用 Task 工具将每个团队成员作为子代理（Subagent）生成：
- `subagent_type: audio-director` — 声音标识、情感基调、音频调色板
- `subagent_type: sound-designer` — 音效规格、音频事件、混音分组
- `subagent_type: technical-artist` — 音频中间件、总线结构、内存预算
- `subagent_type: gameplay-programmer` — 音频管理器、游戏性触发器、自适应音乐

始终在每个代理的提示中提供完整上下文（功能描述、现有音频资产、设计文档引用）。

3. **按顺序编排音频团队**：

### 步骤 1：音频方向（audio-director）
生成 `audio-director` 代理以：
- 定义该功能/区域的声音标识
- 指定情感基调和音频调色板
- 设定音乐方向（自适应层级、分轨 Stems、过渡方式）
- 定义音频优先级和混音目标
- 建立自适应音频规则（战斗强度、探索、紧张感）

### 步骤 2：音效设计（sound-designer）
生成 `sound-designer` 代理以：
- 为每个音频事件创建详细的音效规格
- 定义声音类别（环境音、UI 音、游戏性音效、音乐、对白）
- 指定每个声音的参数（音量范围、音调变化、衰减曲线 Attenuation）
- 规划音频事件列表及触发条件
- 定义混音分组和闪避规则（Ducking Rules）

### 步骤 3：技术实现（technical-artist）
生成 `technical-artist` 代理以：
- 设计音频中间件集成方案（Wwise/FMOD/原生）
- 定义音频总线结构和路由
- 指定各平台音频资产的内存预算
- 规划流式加载（Streaming）与预加载资产策略
- 设计音频驱动的视觉效果（如有）

### 步骤 4：代码集成（gameplay-programmer）
生成 `gameplay-programmer` 代理以：
- 实现音频管理器系统或审查现有系统
- 将音频事件连接到游戏性触发器
- 实现自适应音乐系统（如已指定）
- 设置音频遮挡（Occlusion）和混响区域（Reverb Zones）
- 编写音频事件触发的单元测试

4. **编制音频设计文档**，整合所有团队的输出。

5. **保存至** `Docs/gdd/audio-[feature].md`。

6. **输出摘要**，包含：音频事件数量、预估资产数量、实现任务清单，以及团队成员之间的待决问题。
