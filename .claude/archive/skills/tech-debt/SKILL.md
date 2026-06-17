可以将翻译结果写入 `.claude/skills/tech-debt/SKILL.md` 吗？

```markdown
---
name: tech-debt
description: "跟踪、分类和优先级排序代码库中的技术债务（Technical Debt）。扫描债务指标，维护债务登记册，并推荐偿还计划。"
argument-hint: "[scan|add|prioritize|report]"
user-invocable: true
allowed-tools: Read, Glob, Grep, Write
---
当此技能被调用时：

1. **解析子命令**（Subcommand）：
   - `scan` — 扫描代码库中的技术债务指标
   - `add` — 手动添加新的技术债务条目
   - `prioritize` — 重新排列现有债务登记册的优先级
   - `report` — 生成当前债务状态的汇总报告

2. **对于 `scan`**：
   - 搜索代码库中的债务指标：
     - `TODO` 注释（统计并分类）
     - `FIXME` 注释（伪装成债务的 Bug）
     - `HACK` 注释（需要正规解决方案的临时变通）
     - `@deprecated` 标记
     - 重复的代码块（多个文件中的相似模式）
     - 超过 500 行的文件（潜在的上帝对象，God Object）
     - 超过 50 行的函数（潜在的高复杂度）
   - 对每个发现进行分类：
     - **架构债务（Architecture Debt）**：错误的抽象、缺失的模式、耦合问题
     - **代码质量债务（Code Quality Debt）**：重复、复杂度、命名、缺失类型
     - **测试债务（Test Debt）**：缺失的测试、不稳定的测试（Flaky Test）、未覆盖的边界情况
     - **文档债务（Documentation Debt）**：缺失的文档、过时的文档、未文档化的 API
     - **依赖债务（Dependency Debt）**：过时的包、废弃的 API、版本冲突
     - **性能债务（Performance Debt）**：已知的慢路径、未优化的查询、内存问题
   - 更新 `Docs/tech-debt-register.md` 中的债务登记册

3. **对于 `add`**：
   - 提示输入：描述、类别、受影响的文件、预估修复工作量、不修复的影响
   - 追加到债务登记册

4. **对于 `prioritize`**：
   - 读取债务登记册
   - 按以下公式对每项评分：`(不修复的影响 * 遇到频率) / 修复工作量`
   - 按优先级分数重新排序登记册
   - 推荐应纳入下一个 Sprint 的条目

5. **对于 `report`**：
   - 读取债务登记册
   - 生成汇总统计：
     - 按类别统计的总条目数
     - 总预估修复工作量
     - 自上次报告以来新增 vs 已解决的条目
     - 趋势方向（增长 / 稳定 / 减少）
   - 标记在登记册中超过 3 个 Sprint 未处理的条目
   - 输出报告

### 债务登记册格式

```markdown
## 技术债务登记册
Last updated: [日期]
Total items: [N] | Estimated total effort: [T恤尺码总和]

| ID | Category | Description | Files | Effort | Impact | Priority | Added | Sprint |
|----|----------|-------------|-------|--------|--------|----------|-------|--------|
| TD-001 | [类别] | [描述] | [文件] | [S/M/L/XL] | [Low/Med/High/Critical] | [分数] | [日期] | [修复的 Sprint 或 "Backlog"] |
```

### 规则
- 技术债务本身不是坏事 — 它是一种工具。登记册追踪的是有意识的决定。
- 每条债务条目必须说明接受它的原因（截止日期、原型验证、信息缺失）
- 每个 Sprint 至少应运行一次 `scan` 以捕获新增债务
- 超过 3 个 Sprint 未采取行动的条目应予以修复，或有意识地接受并记录原因
```
