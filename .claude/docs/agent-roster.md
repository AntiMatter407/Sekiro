# 代理名册 (Agent Roster)

以下代理均可使用。每个代理在 `.claude/agents/` 中都有独立的定义文件。请使用最适合当前任务的代理。当任务跨越多个领域时，协调代理（通常是 `producer` 或领域主管）应委派给专业人员。

## 第 1 层 — 领导层代理 (Tier 1 — Leadership Agents, Opus)
| 代理 | 领域 | 使用场景 |
|------|------|----------|
| `creative-director` | 高层愿景 | 重大创意决策、核心支柱冲突、基调/方向把控 |
| `technical-director` | 技术愿景 | 架构决策、技术栈选择、性能策略 |
| `producer` | 制作管理 | Sprint 规划、里程碑跟踪、风险管理、跨部门协调 |

## 第 2 层 — 部门主管代理 (Tier 2 — Department Lead Agents, Sonnet)
| 代理 | 领域 | 使用场景 |
|------|------|----------|
| `game-designer` | 游戏设计 | 机制、系统、成长曲线、经济系统、数值平衡 |
| `lead-programmer` | 代码架构 | 系统设计、代码审查、API 设计、重构 |
| `art-director` | 视觉方向 | 风格指南、美术圣经 (Art Bible)、资产标准、UI/UX 方向 |
| `audio-director` | 音频方向 | 音乐方向、声音调色板、音频实现策略 |
| `narrative-director` | 故事与写作 | 故事弧线、世界观构建、角色设计、对话策略 |
| `qa-lead` | 质量保证 | 测试策略、缺陷分诊 (Bug Triage)、发布就绪、回归计划 |
| `release-manager` | 发布流水线 | 构建管理、版本控制、更新日志、部署、回滚 |
| `localization-lead` | 国际化 | 字符串外部化、翻译流水线、区域测试 (Locale Testing) |

## 第 3 层 — 专业代理 (Tier 3 — Specialist Agents, Sonnet 或 Haiku)
| 代理 | 领域 | 模型 | 使用场景 |
|------|------|------|----------|
| `systems-designer` | 系统设计 | Sonnet | 具体机制实现、公式设计、循环设计 |
| `level-designer` | 关卡设计 | Sonnet | 关卡布局、节奏控制、遭遇设计、流程 |
| `economy-designer` | 经济/平衡 | Sonnet | 资源经济、战利品表 (Loot Table)、成长曲线 |
| `gameplay-programmer` | 游戏逻辑代码 | Sonnet | 功能实现、游戏系统代码 |
| `engine-programmer` | 引擎系统 | Sonnet | 核心引擎、渲染、物理、内存管理 |
| `ai-programmer` | AI 系统 | Sonnet | 行为树 (Behavior Tree)、寻路、NPC 逻辑、状态机 |
| `network-programmer` | 网络同步 | Sonnet | 网络代码 (Netcode)、状态复制、延迟补偿、匹配 |
| `tools-programmer` | 开发工具 | Sonnet | 编辑器扩展、管线工具、调试工具 |
| `ui-programmer` | UI 实现 | Sonnet | UI 框架、屏幕、控件、数据绑定 |
| `technical-artist` | 技术美术 (Tech Art) | Sonnet | 着色器、视觉特效 (VFX)、性能优化、美术管线工具 |
| `sound-designer` | 音效设计 | Haiku | 音效设计文档、音频事件列表、混音说明 |
| `writer` | 对话/背景设定 | Sonnet | 对话编写、背景设定条目、物品描述 |
| `world-builder` | 世界/背景设计 | Sonnet | 世界规则、阵营设计、历史、地理 |
| `qa-tester` | 测试执行 | Haiku | 编写测试用例、缺陷报告、测试清单 |
| `performance-analyst` | 性能分析 | Sonnet | 性能分析 (Profiling)、优化建议、内存分析 |
| `devops-engineer` | 构建/部署 | Haiku | CI/CD、构建脚本、版本控制工作流 |
| `analytics-engineer` | 遥测分析 (Telemetry) | Sonnet | 事件追踪、数据看板、A/B 测试设计 |
| `ux-designer` | 用户体验流程 | Sonnet | 用户流程、线框图、无障碍、输入处理 |
| `prototyper` | 快速原型 | Sonnet | 可丢弃原型、机制测试、可行性验证 |
| `security-engineer` | 安全 | Sonnet | 反作弊、漏洞防护、存档加密、网络安全 |
| `accessibility-specialist` | 无障碍 (Accessibility) | Haiku | WCAG 合规、色盲模式、按键重映射、文本缩放 |
| `live-ops-designer` | 运营活动 (Live Ops) | Sonnet | 赛季、活动、战斗通行证 (Battle Pass)、留存、实时经济 |
| `community-manager` | 社区运营 | Haiku | 补丁说明、玩家反馈、危机沟通、社区健康 |

## 引擎专属代理 (使用与你的引擎匹配的代理集)

### 引擎主管 (Engine Leads)

| 代理 | 引擎 | 模型 | 使用场景 |
| ---- | ---- | ---- | ---- |
| `unreal-specialist` | Unreal Engine 5 | Sonnet | Blueprint 与 C++ 选择、GAS 概览、UE 子系统、虚幻优化 |

### Unreal Engine 子专家

| 代理 | 子系统 | 模型 | 使用场景 |
| ---- | ---- | ---- | ---- |
| `ue-gas-specialist` | Gameplay Ability System | Sonnet | 能力、游戏效果、属性集、标签、预测 |
| `ue-blueprint-specialist` | Blueprint 架构 | Sonnet | BP/C++ 边界、图表规范、命名、BP 优化 |
| `ue-replication-specialist` | 网络/复制 | Sonnet | 属性复制、RPC、预测、相关性、带宽 |
| `ue-umg-specialist` | UMG/CommonUI | Sonnet | 控件层级、数据绑定、CommonUI 输入、UI 性能 |
