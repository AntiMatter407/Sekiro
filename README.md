<p align="center">
  <h1 align="center">Claude Code Game Studios</h1>
  <p align="center">
    将一个 Claude Code 会话变成一个完整的游戏开发工作室。
    <br />
    48 个代理。37 个工作流。一支协同的 AI 团队。
  </p>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="MIT License"></a>
  <a href=".claude/agents"><img src="https://img.shields.io/badge/agents-48-blueviolet" alt="48 Agents"></a>
  <a href=".claude/skills"><img src="https://img.shields.io/badge/skills-37-green" alt="37 Skills"></a>
  <a href=".claude/hooks"><img src="https://img.shields.io/badge/hooks-8-orange" alt="8 Hooks"></a>
  <a href=".claude/rules"><img src="https://img.shields.io/badge/rules-11-red" alt="11 Rules"></a>
  <a href="https://docs.anthropic.com/en/docs/claude-code"><img src="https://img.shields.io/badge/built%20for-Claude%20Code-f5f5f5?logo=anthropic" alt="Built for Claude Code"></a>
  <a href="https://ko-fi.com/donchitos"><img src="https://img.shields.io/badge/Ko--fi-Support%20this%20project-ff5e5b?logo=ko-fi&logoColor=white" alt="Ko-fi"></a>
</p>

---

## 为什么有这个项目

用 AI 独立开发游戏非常强大——但一个聊天会话缺乏结构。没有人阻止你硬编码魔法数字、跳过设计文档、或者写出面条代码。没有 QA 审查、没有设计评审、没有人问"这真的符合游戏愿景吗？"

**Claude Code Game Studios** 通过给你的 AI 会话赋予真实工作室的结构来解决这个问题。你得到的不是一个通用助手，而是 48 个按工作室层级组织的专业代理——总监守护愿景，部门负责人掌控各自领域，专家负责具体执行。每个代理都有明确的职责、上报路径和质量关卡。

结果是：你仍然做出每一个决定，但现在你有一个团队会提出正确的问题、尽早发现错误，并让你的项目从最初的头脑风暴到发布都保持有序。

---

## 目录

- [包含内容](#包含内容)
- [工作室层级](#工作室层级)
- [斜杠命令](#斜杠命令)
- [快速开始](#快速开始)
- [升级](#升级)
- [项目结构](#项目结构)
- [工作原理](#工作原理)
- [设计理念](#设计理念)
- [自定义](#自定义)
- [平台支持](#平台支持)
- [社区](#社区)
- [许可证](#许可证)

---

## 包含内容

| 类别 | 数量 | 描述 |
|------|------|------|
| **代理** | 48 | 覆盖设计、编程、美术、音频、叙事、QA 和制作的专业子代理 |
| **技能** | 37 | 常用工作流的斜杠命令（`/start`、`/sprint-plan`、`/code-review`、`/brainstorm` 等） |
| **钩子** | 8 | 在提交、推送、资源变更、会话生命周期、代理审计和缺口检测时自动验证 |
| **规则** | 11 | 在编辑游戏逻辑、引擎、AI、UI、网络代码等时强制执行的路径范围编码标准 |
| **模板** | 29 | GDD、ADR、冲刺计划、经济模型、阵营设计等文档模板 |

## 工作室层级

代理分为三个层级，与真实工作室的运作方式一致：

```
第一层级 — 总监 (Opus)
  creative-director    technical-director    producer

第二层级 — 部门负责人 (Sonnet)
  game-designer        lead-programmer       art-director
  audio-director       narrative-director    qa-lead
  release-manager      localization-lead

第三层级 — 专家 (Sonnet/Haiku)
  gameplay-programmer  engine-programmer     ai-programmer
  network-programmer   tools-programmer      ui-programmer
  systems-designer     level-designer        economy-designer
  technical-artist     sound-designer        writer
  world-builder        ux-designer           prototyper
  performance-analyst  devops-engineer       analytics-engineer
  security-engineer    qa-tester             accessibility-specialist
  live-ops-designer    community-manager
```

### 引擎专家

模板包含三大引擎的代理集。使用与你项目匹配的代理集：

| 引擎 | 负责代理 | 子专家 |
|------|----------|--------|
| **Godot 4** | `godot-specialist` | GDScript、Shaders、GDExtension |
| **Unity** | `unity-specialist` | DOTS/ECS、Shaders/VFX、Addressables、UI Toolkit |
| **Unreal Engine 5** | `unreal-specialist` | GAS、Blueprints、Replication、UMG/CommonUI |

## 斜杠命令

在 Claude Code 中输入 `/` 即可访问全部 37 个技能：

**评审与分析**
`/design-review` `/code-review` `/balance-check` `/asset-audit` `/scope-check` `/perf-profile` `/tech-debt`

**制作**
`/sprint-plan` `/milestone-review` `/estimate` `/retrospective` `/bug-report`

**项目管理**
`/start` `/project-stage-detect` `/reverse-document` `/gate-check` `/map-systems` `/design-system`

**发布**
`/release-checklist` `/launch-checklist` `/changelog` `/patch-notes` `/hotfix`

**创意**
`/brainstorm` `/playtest-report` `/prototype` `/onboard` `/localize`

**团队协作**（在单个功能上协调多个代理）
`/team-combat` `/team-narrative` `/team-ui` `/team-release` `/team-polish` `/team-audio` `/team-level`

## 快速开始

### 前置要求

- [Git](https://git-scm.com/)
- [Claude Code](https://docs.anthropic.com/en/docs/claude-code)（`npm install -g @anthropic-ai/claude-code`）
- **推荐**：[jq](https://jqlang.github.io/jq/)（用于钩子验证）和 Python 3（用于 JSON 验证）

所有钩子在缺少可选工具时会优雅降级——不会导致任何问题，只是失去验证功能。

### 安装

1. **克隆或用作模板**：
   ```bash
   git clone https://github.com/Donchitos/Claude-Code-Game-Studios.git my-game
   cd my-game
   ```

2. **打开 Claude Code** 并启动会话：
   ```bash
   claude
   ```

3. **运行 `/start`** ——系统会询问你当前的状态（毫无头绪、模糊概念、清晰设计、已有工作），然后引导你进入正确的工作流。不做任何假设。

   如果你已经知道自己需要什么，也可以直接跳转到特定技能：
   - `/brainstorm` — 从零开始探索游戏创意
   - `/setup-engine godot 4.6` — 如果你已经确定引擎，配置你的引擎
   - `/project-stage-detect` — 分析已有项目

## 升级

已经在使用旧版本的模板？请查看 [UPGRADING.md](UPGRADING.md)，获取逐步迁移指南、版本变更详情，以及哪些文件可以安全覆盖、哪些需要手动合并。

## 项目结构

```
CLAUDE.md                           # 主配置文件
.claude/
  settings.json                     # 钩子、权限、安全规则
  agents/                           # 48 个代理定义（Markdown + YAML frontmatter）
  skills/                           # 37 个斜杠命令（每个技能一个子目录）
  hooks/                            # 8 个钩子脚本（Bash，跨平台）
  rules/                            # 11 个路径范围的编码标准
  docs/
    quick-start.md                  # 详细使用指南
    agent-roster.md                 # 完整代理表及领域
    agent-coordination-map.md       # 委派和上报路径
    setup-requirements.md           # 前置要求和平台说明
    templates/                      # 28 个文档模板
src/                                # 游戏源代码
assets/                             # 美术、音频、VFX、着色器、数据文件
design/                             # GDD、叙事文档、关卡设计
docs/                               # 技术文档和 ADR
tests/                              # 测试套件
tools/                              # 构建和流水线工具
prototypes/                         # 一次性原型（与 src/ 隔离）
production/                         # 冲刺计划、里程碑、发布跟踪
```

## 工作原理

### 代理协调

代理遵循结构化的委派模型：

1. **垂直委派** ——总监委派给部门负责人，部门负责人委派给专家
2. **横向协商** ——同层级代理可以相互协商，但不能做出跨领域的约束性决策
3. **冲突解决** ——分歧上报至共同的上级（设计冲突上报至 `creative-director`，技术冲突上报至 `technical-director`）
4. **变更传播** ——跨部门的变更由 `producer` 协调
5. **领域边界** ——代理不修改其领域之外的文件，除非有明确委派

### 协作，而非自主

这**不是**一个自动驾驶系统。每个代理遵循严格的协作协议：

1. **提问** ——代理在提出解决方案之前先提问
2. **展示选项** ——代理展示 2-4 个方案及其利弊
3. **你做决定** ——用户始终做出最终决定
4. **草稿** ——代理在最终确认前展示工作成果
5. **审批** ——没有你的确认，不会写入任何内容

你始终掌控全局。代理提供结构和专业知识，而不是自主行动。

### 自动化安全

**钩子** 在每次会话中自动运行：

| 钩子 | 触发条件 | 功能 |
|------|----------|------|
| `validate-commit.sh` | `git commit` | 检查硬编码值、TODO 格式、JSON 有效性、设计文档章节 |
| `validate-push.sh` | `git push` | 向受保护分支推送时发出警告 |
| `validate-assets.sh` | 在 `assets/` 中写入文件 | 验证命名规范和 JSON 结构 |
| `session-start.sh` | 会话打开 | 加载冲刺上下文和最近的 git 活动 |
| `detect-gaps.sh` | 会话打开 | 检测新项目（建议运行 `/start`）以及代码/原型存在但缺少文档的情况 |
| `pre-compact.sh` | 上下文压缩 | 保留会话进度记录 |
| `session-stop.sh` | 会话关闭 | 记录工作成果 |
| `log-agent.sh` | 代理生成 | 所有子代理调用的审计日志 |

`settings.json` 中的**权限规则**自动允许安全操作（git status、测试运行），阻止危险操作（强制推送、`rm -rf`、读取 `.env` 文件）。

### 路径范围规则

编码标准根据文件位置自动强制执行：

| 路径 | 强制执行内容 |
|------|-------------|
| `src/gameplay/**` | 数据驱动值、delta time 使用、禁止 UI 引用 |
| `src/core/**` | 热路径零分配、线程安全、API 稳定性 |
| `src/ai/**` | 性能预算、可调试性、数据驱动参数 |
| `src/networking/**` | 服务器权威、版本化消息、安全性 |
| `src/ui/**` | 不持有游戏状态、支持本地化、无障碍访问 |
| `design/gdd/**` | 必须包含 8 个章节、公式格式、边界情况 |
| `tests/**` | 测试命名、覆盖率要求、测试夹具模式 |
| `prototypes/**` | 放宽标准、需要 README、记录假设 |

## 设计理念

本模板基于专业游戏开发实践：

- **MDA 框架** ——机制、动态、美学分析用于游戏设计
- **自我决定理论** ——自主性、能力感、关联性用于玩家动机
- **心流状态设计** ——挑战与技能的平衡用于玩家参与度
- **巴特尔玩家类型** ——目标受众定位与验证
- **验证驱动开发** ——先写测试，再实现功能

## 自定义

这是一个**模板**，不是锁定的框架。一切都可以自定义：

- **添加/删除代理** ——删除不需要的代理文件，为你的领域添加新代理
- **编辑代理提示词** ——调整代理行为，添加项目特定知识
- **修改技能** ——调整工作流以匹配你的团队流程
- **添加规则** ——为你的项目目录结构创建新的路径范围规则
- **调整钩子** ——调整验证严格程度，添加新检查
- **选择引擎** ——使用 Godot、Unity 或 Unreal 代理集（或都不用）

## 平台支持

已在 **Windows 10** + Git Bash 上测试。所有钩子使用 POSIX 兼容模式（`grep -E`，而非 `grep -P`），并为缺少的工具提供回退方案。在 macOS 和 Linux 上无需修改即可使用。

## 社区

- **讨论区** —— [GitHub Discussions](https://github.com/Donchitos/Claude-Code-Game-Studios/discussions) 用于提问、分享想法和展示你的作品
- **问题反馈** —— [Bug 报告和功能请求](https://github.com/Donchitos/Claude-Code-Game-Studios/issues)

---

*本项目正在积极开发中。代理架构、技能和协调系统已经成熟可用——但还有更多功能即将推出。*

## 许可证

MIT 许可证。详见 [LICENSE](LICENSE)。
