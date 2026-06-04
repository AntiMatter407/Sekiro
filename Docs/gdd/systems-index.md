# 系统索引：只狼战斗系统迁移

> **状态**：草稿
> **创建日期**：2026-05-30
> **最后更新**：2026-05-30
> **来源概念**：Docs/gdd/game-concept.md

---

## 概述

本项目将《只狼》核心战斗机制完整复刻到 UE5.2。第一阶段实现主角「狼」与一个 Boss 的完整战斗闭环。
所有系统围绕四个游戏支柱运转：刀刃对抗、精确节奏、垂直机动、死亡与回生。
核心循环为：移动→遭遇→攻防博弈→架势崩坏→忍杀。

---

## 系统清单

| # | 系统名称 | 类别 | 优先级 | 状态 | 依赖 |
|---|----------|------|--------|------|------|
| 1 | 输入系统 | 核心 | MVP | 设计中 | [input-system.md](input-system.md) |
| 2 | 碰撞检测 | 核心 | MVP | 设计中 | [collision-detection-system.md](collision-detection-system.md) |
| 3 | 角色移动 | 核心 | MVP | 设计中 | [movement-system.md](movement-system.md) |
| 4 | 动画系统 | 核心 | MVP | 设计中 | [animation-system.md](animation-system.md) |
| 5 | 数据配置 | 核心 | MVP | 设计中 | [data-config-system.md](data-config-system.md) |
| 6 | 锁定系统 | 核心 | MVP | 设计中 | [lockon-system.md](lockon-system.md) |
| 7 | 钩绳系统 | 玩法 | MVP | 设计中 | [grappling-hook-system.md](grappling-hook-system.md) |
| 8 | HP & 架势 | 玩法 | MVP | 设计中 | [hp-posture-system.md](hp-posture-system.md) |
| 9 | 弹刀系统 | 玩法 | MVP | 设计中 | [deflect-system.md](deflect-system.md) |
| 10 | 闪避/识破 | 玩法 | MVP | 设计中 | [dodge-mikiri-system.md](dodge-mikiri-system.md) |
| 11 | 普攻连段 | 玩法 | MVP | 设计中 | [attack-combo-system.md](attack-combo-system.md) |
| 12 | 伤害计算 | 玩法 | MVP | 设计中 | [damage-calculation-system.md](damage-calculation-system.md) |
| 13 | 忍杀 | 玩法 | MVP | 设计中 | [deathblow-system.md](deathblow-system.md) |
| 14 | 危字攻击 | 玩法 | MVP | 设计中 | [perilous-attack-system.md](perilous-attack-system.md) |
| 15 | 回生 | 玩法 | MVP | 设计中 | [resurrection-system.md](resurrection-system.md) |
| 16 | 伤药葫芦 | 玩法 | MVP | 设计中 | [healing-gourd-system.md](healing-gourd-system.md) |
| 17 | Boss AI | 玩法 | MVP | 设计中 | [boss-ai-system.md](boss-ai-system.md) |
| 18 | 战斗 HUD | UI | MVP | 设计中 | [battle-hud-system.md](battle-hud-system.md) |
| 19 | 战斗音效 | 音频 | MVP | 设计中 | [battle-sfx-system.md](battle-sfx-system.md) |
| 20 | 战斗特效 | UI | MVP | 设计中 | [battle-vfx-system.md](battle-vfx-system.md) |

---

## 类别

| 类别 | 描述 |
|------|------|
| **核心** | 所有系统依赖的基础框架（输入、碰撞、移动、动画、数据） |
| **玩法** | 构成战斗体验的游戏机制系统 |
| **UI** | 面向玩家的信息展示 |
| **音频** | 战斗音效反馈 |

---

## 优先级层级

所有 20 个系统均为 **MVP** —— 缺任何一个，狼 vs Boss 的完整战斗闭环无法运行。

---

## 依赖关系图

### 基础层（无依赖）

1. **输入系统** — 所有玩家操作的起点，按键→动作映射
2. **碰撞检测** — 攻击盒/受击盒的底层物理接口
3. **角色移动** — 走/跑/跳/蹲，所有空间交互的载体
4. **动画系统** — AnimBP + Montage，所有动作表现的引擎
5. **数据配置** — DataAsset/DataTable，所有数值的来源

### 核心层（依赖基础层）

6. **锁定系统** — 依赖：输入、移动
7. **钩绳系统** — 依赖：移动、动画
8. **HP & 架势** — 依赖：数据配置
9. **弹刀系统** — 依赖：输入、碰撞、动画
10. **闪避/识破** — 依赖：输入、碰撞、动画、锁定
11. **普攻连段** — 依赖：输入、碰撞、动画、锁定

### 功能层（依赖核心层）

12. **伤害计算** — 依赖：碰撞、HP/架势、数据配置
13. **忍杀** — 依赖：架势、动画、伤害
14. **危字攻击** — 依赖：碰撞、动画、HUD
15. **回生** — 依赖：HP、架势、动画
16. **伤药葫芦** — 依赖：HP、动画、数据配置
17. **Boss AI** — 依赖：碰撞、HP/架势、弹刀、普攻、动画、锁定

### 表现层（依赖功能层）

18. **战斗 HUD** — 依赖：HP/架势、锁定、葫芦、危字
19. **战斗音效** — 依赖：弹刀、忍杀、危字
20. **战斗特效** — 依赖：弹刀、忍杀、危字

---

## 推荐设计顺序

| 顺序 | 系统 | 层级 | 预估工作量 |
|------|------|------|-----------|
| 1 | 数据配置 | 基础层 | S |
| 2 | 输入系统 | 基础层 | S |
| 3 | 碰撞检测 | 基础层 | M |
| 4 | 角色移动 | 基础层 | M |
| 5 | 动画系统 | 基础层 | L |
| 6 | 锁定系统 | 核心层 | S |
| 7 | HP & 架势 | 核心层 | M |
| 8 | 弹刀系统 | 核心层 | L |
| 9 | 闪避/识破 | 核心层 | M |
| 10 | 普攻连段 | 核心层 | M |
| 11 | 钩绳系统 | 核心层 | M |
| 12 | 伤害计算 | 功能层 | S |
| 13 | 忍杀 | 功能层 | M |
| 14 | 危字攻击 | 功能层 | M |
| 15 | 伤药葫芦 | 功能层 | S |
| 16 | 回生 | 功能层 | M |
| 17 | 战斗 HUD | 表现层 | M |
| 18 | 战斗音效 | 表现层 | M |
| 19 | 战斗特效 | 表现层 | M |
| 20 | Boss AI | 功能层 | L |

> 工作量估算：S = 1 会话，M = 2-3 会话，L = 4+ 会话

---

## 循环依赖

- 危字攻击 依赖 战斗 HUD，战斗 HUD 的危字提示部分依赖危字攻击的类型定义 — 通过先定义危字类型枚举（数据配置层）打破循环。

---

## 高风险系统

| 系统 | 风险类型 | 风险描述 | 缓解措施 |
|------|---------|---------|---------|
| 弹刀系统 | 技术/设计 | 精确时机窗口的手感是只狼战斗的灵魂，UE 碰撞检测精度和输入延迟可能影响体验 | 最早开始原型验证，定义帧级时间窗口参数 |
| 动画系统 | 技术 | 动作取消链（Cancel Chain）复杂，根运动与碰撞的同步 | 先做精简版动画状态机，逐步添加取消规则 |
| Boss AI | 设计/范围 | 行为状态机和阶段转换复杂度容易膨胀 | 第一个 Boss 选用行为模式相对简单的，先完成完整闭环再扩展 |
| 钩绳系统 | 技术 | 钩点检测、飞行路径、落地衔接与 UE 移动组件集成 | 可降级为预设钩点方案 |

---

## 进度追踪

| 指标 | 数量 |
|------|------|
| 已识别系统总数 | 20 |
| 已开始设计文档 | 20 |
| 已审核设计文档 | 0 |
| 已批准设计文档 | 0 |
| 已设计 MVP 系统 | 20/20 |

---

## 后续步骤

- [x] 按设计顺序逐个编写 GDD（`/design-system [系统名称]`）
- [x] 先设计碰撞检测、动画、弹刀三个高风险系统
- [ ] 对每篇完成的 GDD 运行 `/design-review`
- [ ] 弹刀系统原型验证（`/prototype deflect`）
- [ ] MVP 系统设计完成后运行 `/gate-check pre-production`
