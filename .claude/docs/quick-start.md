# 游戏工作室代理架构 — 快速入门指南（Quick Start Guide）

## 这是什么？

这是一套完整的面向游戏开发的 Claude Code 代理架构。它将 48 个专业 AI 代理组织为反映真实游戏开发团队的工作室层级结构，具有明确的职责分工、委派规则和协调协议。它包含面向 Godot、Unity 和 Unreal 的引擎专家代理——每个引擎都配备了专用于主要引擎子系统的子专家。所有设计代理和模板均植根于成熟的游戏设计理论（MDA Framework 自我决定理论 Self-Determination Theory、心流状态 Flow State、Bartle 玩家类型理论 Bartle Player Types）。请使用与你项目匹配的引擎代理集。

## 使用方法

### 1. 理解层级结构

代理分为三个层级：

- **第一层（Tier 1, Opus）**：负责高层决策的总监（Director）
  - `creative-director` — 愿景与创意冲突解决
  - `technical-director` — 架构与技术决策
  - `producer` — 排期、协调与风险管理

- **第二层（Tier 2, Sonnet）**：负责各自领域的部门主管（Department Lead）
  - `game-designer`、`lead-programmer`、`art-director`、`audio-director`、
    `narrative-director`、`qa-lead`、`release-manager`、`localization-lead`

- **第三层（Tier 3, Sonnet/Haiku）**：在各自领域内执行工作的专家（Specialist）
  - 设计师、程序员、美术师、编剧、测试员、工程师

### 2. 为任务选择合适的代理

问自己："在真实工作室中，哪个部门会处理这件事？"

| 我需要... | 使用此代理 |
|-------------|---------------|
| 设计新机制 | `game-designer` |
| 编写战斗代码 | `gameplay-programmer` |
| 创建着色器（Shader） | `technical-artist` |
| 编写对话 | `writer` |
| 规划下一个冲刺（Sprint） | `producer` |
| 审查代码质量 | `lead-programmer` |
| 编写测试用例 | `qa-tester` |
| 设计关卡 | `level-designer` |
| 修复性能问题 | `performance-analyst` |
| 搭建 CI/CD | `devops-engineer` |
| 设计战利品表（Loot Table） | `economy-designer` |
| 解决创意冲突 | `creative-director` |
| 做出架构决策 | `technical-director` |
| 管理发布 | `release-manager` |
| 准备待翻译字符串 | `localization-lead` |
| 快速验证机制想法 | `prototyper` |
| 审查代码安全性 | `security-engineer` |
| 检查无障碍（Accessibility）合规性 | `accessibility-specialist` |
| 获取虚幻引擎（Unreal Engine）建议 | `unreal-specialist` |
| 获取 Unity 建议 | `unity-specialist` |
| 获取 Godot 建议 | `godot-specialist` |
| 设计 GAS 能力/效果 | `ue-gas-specialist` |
| 定义蓝图（Blueprint）/C++ 边界 | `ue-blueprint-specialist` |
| 实现 UE 网络复制（Replication） | `ue-replication-specialist` |
| 构建 UMG/CommonUI 控件 | `ue-umg-specialist` |
| 设计 DOTS/ECS 架构 | `unity-dots-specialist` |
| 编写 Unity 着色器/VFX | `unity-shader-specialist` |
| 管理 Addressable 资产 | `unity-addressables-specialist` |
| 构建 UI Toolkit/UGUI 界面 | `unity-ui-specialist` |
| 编写地道 GDScript | `godot-gdscript-specialist` |
| 创建 Godot 着色器 | `godot-shader-specialist` |
| 构建 GDExtension 模块 | `godot-gdextension-specialist` |
| 规划线上活动与赛季 | `live-ops-designer` |
| 为玩家编写补丁说明 | `community-manager` |
| 头脑风暴新游戏想法 | 使用 `/brainstorm` 技能 |

### 3. 使用斜杠命令处理常见任务

| 命令 | 功能 |
|---------|-------------|
| `/start` | 首次入门引导 — 询问你的现状，引导至正确的工作流 |
| `/design-review` | 审查设计文档 |
| `/code-review` | 审查代码质量与架构 |
| `/playtest-report` | 创建或分析试玩反馈 |
| `/balance-check` | 分析游戏平衡数据 |
| `/sprint-plan` | 创建或更新冲刺计划 |
| `/architecture-decision` | 创建架构决策记录（ADR） |
| `/asset-audit` | 审计资产合规性 |
| `/milestone-review` | 审查里程碑进度 |
| `/onboard` | 为特定角色生成入职文档 |
| `/prototype` | 搭建一次性原型 |
| `/release-checklist` | 验证发布前清单 |
| `/changelog` | 从 Git 历史生成更新日志 |
| `/retrospective` | 运行冲刺/里程碑回顾 |
| `/estimate` | 生成结构化的工作量估算 |
| `/hotfix` | 带审计追踪的紧急修复 |
| `/tech-debt` | 扫描、跟踪并优先排序技术债 |
| `/scope-check` | 对照计划检测范围蔓延（Scope Creep） |
| `/localize` | 本地化扫描、提取、验证 |
| `/perf-profile` | 性能分析与瓶颈识别 |
| `/gate-check` | 验证阶段就绪状态（PASS/CONCERNS/FAIL） |
| `/project-stage-detect` | 分析项目状态、检测阶段、识别缺口 |
| `/reverse-document` | 从现有代码生成设计/架构文档 |
| `/setup-engine` | 配置引擎和版本，填充参考文档 |
| `/map-systems` | 将概念拆解为系统，映射依赖，指导按系统编写 GDD |
| `/design-system` | 针对单个游戏系统的引导式逐章节 GDD 编写 |
| `/team-combat` | 编排完整的战斗团队流程 |
| `/team-narrative` | 编排完整的叙事团队流程 |
| `/team-ui` | 编排完整的 UI 团队流程 |
| `/team-release` | 编排完整的发布团队流程 |
| `/team-polish` | 编排完整的打磨团队流程 |
| `/team-audio` | 编排完整的音频团队流程 |
| `/team-level` | 编排完整的关卡创建流程 |
| `/launch-checklist` | 完整的上线就绪验证 |
| `/patch-notes` | 生成面向玩家的补丁说明 |
| `/brainstorm` | 从零开始的引导式游戏概念头脑风暴 |

### 4. 使用模板创建新文档

模板位于 `.claude/docs/templates/`：

- `game-design-document.md` — 用于新机制和系统
- `architecture-decision-record.md` — 用于技术决策
- `risk-register-entry.md` — 用于新风险
- `narrative-character-sheet.md` — 用于新角色
- `test-plan.md` — 用于功能测试计划
- `sprint-plan.md` — 用于冲刺计划
- `milestone-definition.md` — 用于新里程碑
- `level-design-document.md` — 用于新关卡
- `game-pillars.md` — 用于核心设计支柱
- `art-bible.md` — 用于视觉风格参考
- `technical-design-document.md` — 用于按系统的技术设计
- `post-mortem.md` — 用于项目/里程碑复盘
- `sound-bible.md` — 用于音频风格参考
- `release-checklist-template.md` — 用于平台发布清单
- `changelog-template.md` — 用于面向玩家的更新日志
- `release-notes.md` — 用于面向玩家的发布说明
- `incident-response.md` — 用于线上事件响应预案
- `game-concept.md` — 用于初始游戏概念（MDA、SDT、Flow、Bartle）
- `pitch-document.md` — 用于向利益相关者推介游戏
- `economy-model.md` — 用于虚拟经济设计（消耗/产出模型）
- `faction-design.md` — 用于阵营身份、设定与玩法角色
- `systems-index.md` — 用于系统拆解与依赖映射
- `project-stage-report.md` — 用于项目阶段检测输出
- `design-doc-from-implementation.md` — 用于将现有代码逆向文档化为 GDD
- `architecture-doc-from-code.md` — 用于将代码逆向文档化为架构文档
- `concept-doc-from-prototype.md` — 用于将原型逆向文档化为概念文档

### 5. 遵循协调规则

1. 工作沿层级向下流动：总监 → 主管 → 专家
2. 冲突沿层级向上升级
3. 跨部门工作由 `producer` 协调
4. 代理未经委派不得修改其领域之外的文件
5. 所有决策必须记录在案

## 新项目的第一步

**不知道从哪里开始？** 运行 `/start`。它会询问你的现状并引导至正确的工作流。不会对你的游戏、引擎或经验水平做任何假设。

如果你已经清楚自己需要什么，可以直接跳到对应的路径：

### 路径 A："我不知道要做什么"

1. **运行 `/start`**（或 `/brainstorm open`）— 引导式创意探索：
   你对什么感兴趣、你玩过什么、你的限制条件是什么
   - 生成 3 个概念，帮你选择一个，定义核心循环和支柱
   - 产出游戏概念文档并推荐引擎
2. **配置引擎** — 运行 `/setup-engine`（使用头脑风暴的推荐结果）
   - 配置 `CLAUDE.md`，检测知识缺口，填充参考文档
   - 创建 `.claude/docs/technical-preferences.md`，包含命名约定、性能预算和引擎特定的默认值
   - 如果引擎版本比 LLM 的训练数据更新，它会从网上获取最新文档，确保代理建议正确的 API
3. **验证概念** — 运行 `/design-review Docs/gdd/game-concept.md`
4. **拆解为系统** — 运行 `/map-systems` 映射所有系统和依赖
5. **设计每个系统** — 运行 `/design-system [system-name]`（或 `/map-systems next`）按依赖顺序编写 GDD
6. **测试核心循环** — 运行 `/prototype [core-mechanic]`
7. **试玩验证** — 运行 `/playtest-report` 验证假设
8. **规划第一个冲刺** — 运行 `/sprint-plan new`
9. 开始构建

### 路径 B："我知道要做什么"

如果你已有游戏概念和引擎选择：

1. **配置引擎** — 运行 `/setup-engine [engine] [version]`
   （例如 `/setup-engine godot 4.6`）— 同时创建技术偏好
2. **编写游戏支柱** — 委派给 `creative-director`
3. **拆解为系统** — 运行 `/map-systems` 枚举系统和依赖
4. **设计每个系统** — 运行 `/design-system [system-name]` 按依赖顺序编写 GDD
5. **创建初始架构决策记录** — 运行 `/architecture-decision`
6. **创建第一个里程碑**，存放在 `production/milestones/`
7. **规划第一个冲刺** — 运行 `/sprint-plan new`
8. 开始构建

### 路径 C："我知道游戏但不确定引擎"

如果你有概念但不知道哪个引擎适合：

1. **运行 `/setup-engine`** 不带参数 — 它会询问你游戏的需求
   （2D/3D、目标平台、团队规模、语言偏好），并根据你的回答推荐引擎
2. 从路径 B 的第 2 步开始继续

### 路径 D："我已有现有项目"

如果你已有设计文档、原型或代码：

1. **运行 `/start`**（或 `/project-stage-detect`）— 分析现有内容、
   识别缺口并推荐下一步
2. **按需配置引擎** — 如未配置则运行 `/setup-engine`
3. **验证阶段就绪状态** — 运行 `/gate-check` 查看当前状态
4. **规划下一个冲刺** — 运行 `/sprint-plan new`

## 文件结构参考

```
CLAUDE.md                          -- 主配置文件（优先阅读，约 60 行）
.claude/
  settings.json                    -- Claude Code 钩子和项目设置
  agents/                          -- 48 个代理定义（YAML frontmatter）
  skills/                          -- 37 个斜杠命令定义（YAML frontmatter）
  hooks/                           -- 8 个钩子脚本（.sh），由 settings.json 接入
  rules/                           -- 11 个路径特定的规则文件
  Docs/
    quick-start.md                 -- 本文件
    technical-preferences.md       -- 项目特定的标准（由 /setup-engine 填充）
    coding-standards.md            -- 编码与设计文档标准
    coordination-rules.md          -- 代理协调规则
    context-management.md          -- 上下文预算与压缩指令
    review-workflow.md             -- 审查与签核流程
    directory-structure.md         -- 项目目录布局
    agent-roster.md                -- 完整的代理列表（含层级）
    skills-reference.md            -- 所有斜杠命令
    rules-reference.md             -- 路径特定的规则
    hooks-reference.md             -- 活跃的钩子
    agent-coordination-map.md      -- 完整的委派与工作流图
    setup-requirements.md          -- 系统前置要求（Git Bash、jq、Python）
    settings-local-template.md     -- 个人 settings.local.json 指南
    hooks-reference/               -- 钩子文档与 Git Hook 示例
    templates/                     -- 28 个文档模板
```
