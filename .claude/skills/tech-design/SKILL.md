---
name: tech-design
description: "根据需求文档编写技术方案，生成架构文档，并协调专业 Agent 派发实现任务。"
---

# /tech-design — 技术方案设计与任务调度

## 触发方式

| 命令 | 作用 |
|------|------|
| `/tech-design <需求名称>` | 为整个需求编写技术方案并派发 |
| `/tech-design <需求名称> <子任务ID>` | 只针对某个子任务编写技术方案 |
| `/tech-design <需求名称> --dispatch` | 跳过方案编写，直接进入任务派发 |
| `/tech-design list` | 列出所有技术方案及对应需求状态 |

## 工作流

```
Docs/breakdown/<需求>.md
        │
        ├── /tech-design <需求>            → 完整技术方案 + 全部派发
        │
        └── /tech-design <需求> <子任务ID>  → 聚焦子任务方案 + 单任务派发
                │
                ▼
          1. 读取需求文档，提取目标子任务
          2. 探索相关代码
          3. 编写子任务技术方案 → Docs/tech-designs/<需求>-<子任务ID>.md
          4. 确认 → 派发对应 Agent → 完成该子任务
          5. 完成后更新 breakdown 文档该任务状态
```

子任务有依赖时，先完成依赖任务再处理后续。一个需求按子任务逐个推进：
```
breakdown: 1.1 → 1.2 → 2.1
tech-design 1.1 → agent 完成 → tech-design 1.2 → agent 完成 → tech-design 2.1 ...
```

## 技术方案文档模板

### 完整需求方案（`/tech-design <需求>`）

新建方案时，在 `Docs/tech-designs/<slug>.md` 创建：

```markdown
# [需求名称] — 技术方案

| 需求文档 | 状态 | 创建 | 更新 |
|-----------|------|------|------|
| [需求名称](../breakdown/<slug>.md) | 🟡 设计中 | YYYY-MM-DD | YYYY-MM-DD |

## 架构设计
（类图、模块划分、数据流）

## 涉及文件

| 文件 | 操作 | 说明 |
|------|------|------|
| Source/Sekiro/Core/SKXxx.h | 新建 | XXX |

## API 设计
（对外接口、函数签名、关键参数）

## 数据流
（关键数据在模块间的流转路径）

## 依赖与风险
（依赖的外部模块、插件、未解决的阻塞项）

## 任务→Agent 映射

| 子任务 ID | 任务描述 | 负责 Agent | 状态 |
|-----------|---------|-----------|------|
| 1.1 | XXX | gameplay-programmer | ⬜ |
| 1.2 | YYY | plugin-programmer | ⬜ |

## 变更记录
| 日期 | 变更 |
|------|------|
| YYYY-MM-DD | 创建 |
```

### 子任务方案（`/tech-design <需求> <子任务ID>`）

在 `Docs/tech-designs/<需求>-<子任务ID>.md` 创建轻量方案：

```markdown
# [需求名称] / [子任务ID] — 技术方案

| 需求文档 | 子任务 | 状态 | 创建 |
|-----------|--------|------|------|
| [需求](../breakdown/<slug>.md) | 1.1 | 🔄 进行中 | YYYY-MM-DD |

## 任务描述
（从 breakdown 摘取的任务内容）

## 实现方案
（聚焦该子任务的代码结构、接口设计）

## 涉及文件

| 文件 | 操作 | 说明 |
|------|------|------|
| Source/Sekiro/Combat/SKCombatComp.h | 修改 | 新增 XXX 方法 |

## 负责 Agent
- **Agent**：gameplay-programmer
- **输入**：上述接口设计
- **依赖**：无 / (依赖: 1.1 完成)

## 变更记录
| 日期 | 变更 |
|------|------|
| YYYY-MM-DD | 创建 |
```

## 代码库探索规则

编写技术方案前，必须探索：
1. 读取 `.claude/docs/directory-structure.md` 确认项目结构
2. 搜索涉及模块的现有代码，确认现状
3. 检查 `Plugins/` 中已有插件的能力边界
4. 记录现有代码中需要遵循的模式（命名、类结构）

## 确认机制

### 方案确认
1. AI 读取 breakdown 文档，提取目标子任务
2. 在对话中输出方案草稿（聚焦该子任务的实现方案、涉及文件、接口设计）
3. 用户可以调整方案、修改文件范围
4. 用户确认后，AI 写入 `Docs/tech-designs/<需求>-<子任务ID>.md`
5. 回写 breakdown 文档，标记该任务 🔄 进行中

### Agent 派发确认
1. AI 展示 Agent 工作卡片：

```
### gameplay-programmer
- 子任务：1.1 实现 XXX
- 涉及文件：Source/Sekiro/Combat/SKCombatComp.h, SKCombatComp.cpp
- 输入：Docs/tech-designs/战斗系统-1.1.md
- 依赖：无
```

2. 用户确认后，调用 Agent 执行
3. Agent 完成后，更新 breakdown 和 tech-design 文档状态（🔄 → ✅）

## 操作规则

- **逐子任务推进**：默认每次 `/tech-design` 只处理一个子任务，完成后再下一个
- 子任务有依赖时，必须先完成依赖项
- Agent 完成后自动回写 breakdown 状态
- 同一子任务可多次 `/tech-design` 调整方案并重新派发
- 方案变更时记录到变更记录