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
| `/tech-design <需求名称> --verify <任务ID>` | 手动触发功能验证（编译已通过后） |
| `/tech-design <需求名称> --auto-verify <任务ID>` | 自主验证模式，无需逐项确认，tech-design 全权负责 |
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
| [需求名称](../../../Docs/breakdown/<slug>.md) | 🟡 设计中 | YYYY-MM-DD | YYYY-MM-DD |

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
| [需求](../../../Docs/breakdown/<slug>.md) | 1.1 | 🔄 进行中 | YYYY-MM-DD |

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

## 复杂问题 → 推理分析

遇到以下情况时，**调用 `/reasoning` 技能**代替自行决策：

| 情况 | 说明 |
|------|------|
| 架构方案存在多个可行方向 | 派发多个 expert-agent 并行出方案对比 |
| 方案需要从不同视角审视 | 性能/安全/可扩展性等不同专家视角 |
| 方案需要专业评审 | 一个 agent 出方案，另一个评审 |
| 需求不清晰、方案有争议 | 先推理分析，再派发实现 |

tech-design 中的推理分析流程：

```
tech-design 遇到复杂问题
      │
      ▼
  调用 /reasoning <具体问题>
      │
      ▼
  reasoning 产出方案文档（并发多个 expert-agent）
      │
      ▼
  tech-design 将方案文档作为子任务的技术方案
      → 写入 Docs/tech-designs/<需求>-<子任务ID>.md
      → 按正常流程派发 Agent 实现
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
3. Agent 完成后，先检查改动文件是否在允许范围内（参考「Agent 派发安全规则」）
4. **自动调用 function-validator 验证编译**：
   - 检查编译是否 0 错误通过
   - 编译失败 → 分析错误日志，反馈对应 Agent 修复
   - 编译通过 → 更新 breakdown 和 tech-design 文档状态（🔄 → ✅）
   - **编译通过后提醒用户**：输出「编译通过。`--verify` 手动验证 / `--auto-verify` 自主验证」
5. **功能性验证由用户手动发起**：
   - `--verify`：手动模式，复杂测试需确认方案后执行，用户全程掌控
   - `--auto-verify`：自主模式，用户明确授权后，tech-design 全权负责方案生成、脚本委派、执行，无需逐项确认。用户随时可打断收回控制权
   - 需要测试脚本时，tech-design 协调 script-agent 编写

## 操作规则

- **逐子任务推进**：默认每次 `/tech-design` 只处理一个子任务，完成后再下一个
- 子任务有依赖时，必须先完成依赖项
- Agent 完成后自动回写 breakdown 状态
- 同一子任务可多次 `/tech-design` 调整方案并重新派发
- 方案变更时记录到变更记录

## Agent 派发安全规则

### 派发时必须在 prompt 中明确

每个 Agent 派发时必须附带以下约束，防止 Agent 越界修改：

```
## 文件边界
- 只能修改 Source/Sekiro/ 下的文件
- 禁止修改 Plugins/ 下的任何文件
- 禁止修改 .Build.cs / .Target.cs / .uproject
- 如需插件新接口，停止并反馈，由 plugin-programmer 先行实现
```

### Agent 完成后验证

1. 用 `git diff --stat` 或 `git status` 检查 Agent 实际改动的文件列表
2. 与 tech-design 中「涉及文件」清单对比
3. 发现越界文件（Plugin 目录、构建文件）→ **立即 `git checkout` 逐文件回滚**，不接受该 Agent 输出
4. 合法的意外改动（Agent 清理了无关代码）→ 记录到变更记录

### Agent 改错时的回滚流程

```
git status                     → 列出所有改动文件
git diff --stat                → 确认改动范围
逐文件 git checkout -- <path>  → 回滚越界文件（不用目录级，避免误伤）
保留合法文件 → 手动修复或重新派发
```

**禁止目录级 `git checkout -- <目录>/`**：会误伤该目录下之前的合法改动。始终逐文件回滚。