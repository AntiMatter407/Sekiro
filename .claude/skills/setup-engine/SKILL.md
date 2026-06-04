---
name: setup-engine
description: "配置项目的游戏引擎和版本。将引擎锁定到 CLAUDE.md 中，检测知识缺口，当版本超出 LLM 训练数据时通过 WebSearch 填充引擎参考文档。"
argument-hint: "[engine version] or no args for guided selection"
user-invocable: true
allowed-tools: Read, Glob, Grep, Write, Edit, WebSearch, WebFetch, Task
---

当此技能被调用时：

## 1. 解析参数

三种模式：

- **完整规格**：`/setup-engine godot 4.6` — 已提供引擎和版本
- **仅引擎**：`/setup-engine unity` — 已提供引擎，版本将自动查找
- **无参数**：`/setup-engine` — 完全引导模式（引擎推荐 + 版本选择）

---

## 2. 引导模式（无参数）

如果未指定引擎，运行交互式引擎选择流程：

### 检查现有游戏概念
- 如果存在，读取 `Docs/gdd/game-concept.md` — 提取游戏类型、范围、目标平台、美术风格、团队规模，以及 `/brainstorm` 给出的引擎推荐
- 如果概念不存在，告知用户：
  > "未找到游戏概念。建议先运行 `/brainstorm` 发现你想构建什么 — 它还会推荐引擎。或者告诉我你的游戏想法，我可以帮你选择。"

### 如果用户想在无概念的情况下选择，询问：
1. **什么类型的游戏？**（2D、3D，或两者兼具？）
2. **什么平台？**（PC、移动端、主机、网页？）
3. **团队规模和经验？**（单人新手、单人经验丰富、小团队？）
4. **有强烈的语言偏好吗？**（GDScript、C#、C++、可视化脚本？）
5. **引擎授权预算？**（仅免费，还是可接受商业授权？）

### 给出推荐

使用以下决策矩阵：

| 因素 | Godot 4 | Unity | Unreal Engine 5 |
|------|---------|-------|-----------------|
| **最适用于** | 2D 游戏、小型 3D、单人/小团队 | 移动端、中等规模 3D、跨平台 | 3A 级 3D、照片级写实、大型团队 |
| **语言** | GDScript（+ C#，通过扩展支持 C++） | C# | C++ / Blueprint |
| **费用** | 免费，MIT 许可证 | 收入门槛以下免费 | 收入门槛以下免费，5% 版税 |
| **学习曲线** | 平缓 | 中等 | 陡峭 |
| **2D 支持** | 优秀（原生） | 良好（但以 3D 为优先） | 可行但非理想选择 |
| **3D 质量上限** | 良好（快速提升中） | 非常好 | 业界领先 |
| **Web 导出** | 支持（原生） | 支持（有限制） | 不支持 |
| **主机导出** | 通过第三方 | 支持（需要授权） | 支持 |
| **开源** | 是 | 否 | 源码可用 |

给出排名前 1-2 的推荐及其与用户回答相关联的理由。
让用户自行选择 — 永远不要强制推荐。

---

## 3. 查找当前版本

引擎确定后：

- 如果提供了版本，直接使用
- 如果未提供版本，使用 WebSearch 查找最新稳定版：
  - 搜索：`"[engine] latest stable version [当前年份]"`
  - 与用户确认："最新稳定版 [引擎] 是 [版本]。使用这个吗？"

---

## 4. 更新 CLAUDE.md 技术栈

读取 `CLAUDE.md` 并更新技术栈部分。将 `[CHOOSE]` 占位符替换为实际值：

**Godot:**
```markdown
- **引擎**：Godot [版本]
- **语言**：GDScript（主要），通过 GDExtension 支持 C++（性能关键场景）
- **构建系统**：SCons（引擎），Godot 导出模板
- **资产管线**：Godot 导入系统 + 自定义资源管线
```

**Unity:**
```markdown
- **引擎**：Unity [版本]
- **语言**：C#
- **构建系统**：Unity Build Pipeline
- **资产管线**：Unity Asset Import Pipeline + Addressables
```

**Unreal:**
```markdown
- **引擎**：Unreal Engine [版本]
- **语言**：C++（主要），Blueprint（游戏玩法原型开发）
- **构建系统**：Unreal Build Tool (UBT)
- **资产管线**：Unreal Content Pipeline
```

---

## 5. 填充技术偏好

更新 CLAUDE.md 后，创建或更新 `.claude/docs/technical-preferences.md`，填入引擎对应的默认值。先阅读现有模板，然后填入：

### 引擎和语言部分
- 根据第 4 步的引擎选择填入

### 命名约定（引擎默认值）

**Godot (GDScript):**
- 类名：PascalCase（如 `PlayerController`）
- 变量/函数：snake_case（如 `move_speed`）
- 信号：snake_case 过去时（如 `health_changed`）
- 文件名：snake_case 与类名匹配（如 `player_controller.gd`）
- 场景：PascalCase 与根节点匹配（如 `PlayerController.tscn`）
- 常量：UPPER_SNAKE_CASE（如 `MAX_HEALTH`）

**Unity (C#):**
- 类名：PascalCase（如 `PlayerController`）
- 公共字段/属性：PascalCase（如 `MoveSpeed`）
- 私有字段：_camelCase（如 `_moveSpeed`）
- 方法：PascalCase（如 `TakeDamage()`）
- 文件名：PascalCase 与类名匹配（如 `PlayerController.cs`）
- 常量：PascalCase 或 UPPER_SNAKE_CASE

**Unreal (C++):**
- 类名：带前缀的 PascalCase（`A` 表示 Actor，`U` 表示 UObject，`F` 表示结构体）
- 变量：PascalCase（如 `MoveSpeed`）
- 函数：PascalCase（如 `TakeDamage()`）
- 布尔值：`b` 前缀（如 `bIsAlive`）
- 文件名：与类名匹配但不带前缀（如 `PlayerController.h`）

### 其余部分
- 性能预算：保留为 `[TO BE CONFIGURED]` 并附带建议：
  > "典型目标：60fps / 16.6ms 帧预算。现在设置这些吗？"
- 测试：建议引擎对应的框架（Godot 用 GUT，Unity 用 NUnit 等）
- 禁止模式/允许的库：保留为占位符

### 协作步骤
向用户展示填好的偏好设置：
> "这是 [引擎] 的默认技术偏好。要自定义其中任何一项，还是直接保存默认值？"

等待用户批准后再写入文件。

---

## 6. 确定知识缺口

检查引擎版本是否可能超出 LLM 的训练数据。

**已知的大致覆盖范围**（随着模型更新调整）：
- LLM 知识截止日期：**2025 年 5 月**
- Godot：训练数据可能覆盖到 ~4.3
- Unity：训练数据可能覆盖到 ~2023.x / 6000.x 早期版本
- Unreal：训练数据可能覆盖到 ~5.3 / 5.4 早期版本

将用户选择的版本与这些基线进行比较：

- **在训练数据范围内** → `低风险` — 参考文档可选但推荐
- **接近边界** → `中风险` — 推荐参考文档
- **超出训练数据** → `高风险` — 必须有参考文档

告知用户所属的类别及原因。

---

## 7. 填充引擎参考文档

### 如果在训练数据范围内（低风险）：

创建一个精简的 `Docs/engine-reference/<engine>/VERSION.md`：

```markdown
# [Engine] — 版本参考

| 字段 | 值 |
|------|-----|
| **引擎版本** | [版本] |
| **项目锁定日期** | [今天的日期] |
| **LLM 知识截止** | 2025 年 5 月 |
| **风险等级** | 低 — 版本在 LLM 训练数据范围内 |

## 说明

此引擎版本在 LLM 的训练数据范围内。引擎参考文档为可选项，
但如果代理建议了错误的 API，可稍后添加。

随时运行 `/setup-engine refresh` 来填充完整参考文档。
```

不要创建 breaking-changes.md、deprecated-apis.md 等文件 — 它们会增加上下文成本但价值有限。

### 如果超出训练数据（中风险或高风险）：

通过搜索网络创建完整的参考文档集：

1. **搜索官方迁移/升级指南**：
   - `"[engine] [旧版本] 到 [新版本] 迁移指南"`
   - `"[engine] [版本] breaking changes"`
   - `"[engine] [版本] changelog"`
   - `"[engine] [版本] deprecated API"`

2. **从官方文档获取并提取**：
   - 从知识截止到当前版本之间的每个版本的 breaking changes
   - 已废弃的 API 及其替代方案
   - 新功能和最佳实践

3. **创建完整的参考目录**：
   ```
   Docs/engine-reference/<engine>/
   ├── VERSION.md              # 版本锁定 + 知识缺口分析
   ├── breaking-changes.md     # 逐版本的 breaking changes
   ├── deprecated-apis.md      # "不要用 X → 用 Y" 对照表
   ├── current-best-practices.md  # 知识截止后的新实践
   └── modules/                # 各子系统参考（按需创建）
   ```

4. **使用网络搜索的真实数据填充每个文件**，遵循现有参考文档的格式。每个文件必须有"最后验证：[日期]"标题。

5. **对于模块文件**：仅对发生了重大变化的子系统创建模块。不要创建空白或内容稀少的模块文件。

---

## 8. 更新 CLAUDE.md 导入

更新"引擎版本参考"下的 `@` 导入，指向正确的引擎：

```markdown
## 引擎版本参考

@Docs/engine-reference/<engine>/VERSION.md
```

如果之前的导入指向不同引擎（例如从 Godot 切换到 Unity），请更新。

---

## 9. 更新 Agent 指令

对于所选引擎的专家代理，验证它们是否有"版本感知"部分。如果没有，按照现有 Godot 专家代理的模式添加。

该部分应指示代理：
1. 读取 `Docs/engine-reference/<engine>/VERSION.md`
2. 建议代码前检查已废弃的 API
3. 检查相关版本过渡的 breaking changes
4. 使用 WebSearch 验证不确定的 API

---

## 10. Refresh 子命令

如果以 `/setup-engine refresh` 调用：

1. 读取现有的 `Docs/engine-reference/<engine>/VERSION.md` 获取当前引擎和版本
2. 使用 WebSearch 检查：
   - 自上次验证以来的新引擎发布
   - 更新的迁移指南
   - 新废弃的 API
3. 用新发现更新所有参考文档
4. 更新所有修改文件的"最后验证"日期
5. 报告变更内容

---

## 11. 输出摘要

设置完成后，输出：

```
引擎设置完成
=====================
引擎：            [名称] [版本]
知识风险：        [低/中/高]
参考文档：        [已创建/已跳过]
CLAUDE.md：       [已更新]
技术偏好：        [已创建/已更新]
代理配置：        [已验证]

后续步骤：
1. 查看 Docs/engine-reference/<engine>/VERSION.md
2. [如果来自 /brainstorm] 运行 /map-systems 将概念分解为独立系统
3. [如果来自 /brainstorm] 运行 /design-system 逐系统编写 GDD（引导式，按章节）
4. [如果来自 /brainstorm] 运行 /prototype [核心机制] 测试核心循环
5. [如果是全新开始] 运行 /brainstorm 发现游戏概念
6. 创建第一个里程碑：/sprint-plan new
```

---

## 护栏

- 绝对不要猜测引擎版本 — 始终通过 WebSearch 或用户确认来验证
- 没有询问前绝不要覆盖现有参考文档 — 应追加或更新
- 如果已有不同引擎的参考文档，替换前先询问
- 始终在编辑 CLAUDE.md 之前向用户展示将要修改的内容
- 如果 WebSearch 返回模糊结果，展示给用户并让其决定
