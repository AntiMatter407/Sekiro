---
name: team-release
description: "编排发布团队：协调 release-manager、qa-lead、devops-engineer 和 producer，从候选版本到部署执行完整发布流程。"
argument-hint: "[版本号或 'next']"
user-invocable: true
allowed-tools: Read, Glob, Grep, Write, Edit, Bash, Task, AskUserQuestion, TodoWrite
---
当此技能被调用时，通过结构化管线编排发布团队。

**决策点：** 在每个阶段转换时，使用 `AskUserQuestion` 将子代理的提案以可选项的形式呈现给用户。在对话中展示代理的完整分析，然后用简洁的标签记录决策。用户必须批准后才能进入下一阶段。

## 团队构成
- **release-manager** — 发布分支、版本管理、变更日志、部署
- **qa-lead** — 测试签收、回归测试套件、发布质量门禁
- **devops-engineer** — 构建管线、产物、部署自动化
- **producer** — 放行/中止决策、利益相关者沟通、排期

## 委派方式

使用 Task 工具将每位团队成员作为子代理启动：
- `subagent_type: release-manager` — 发布分支、版本管理、变更日志、部署
- `subagent_type: qa-lead` — 测试签收、回归测试套件、发布质量门禁
- `subagent_type: devops-engineer` — 构建管线、产物、部署自动化
- `subagent_type: producer` — 放行/中止决策、利益相关者沟通

始终在每个代理的提示中提供完整上下文（版本号、里程碑状态、已知问题）。在管线允许的情况下并行启动独立代理（例如，阶段 3 的代理可以同时运行）。

## 管线

### 阶段 1：发布规划
委派给 **producer**：
- 确认所有里程碑验收标准均已满足
- 识别从本次发布中推迟的范围项
- 设定目标发布日期并通知团队
- 输出：附带范围确认的发布授权

### 阶段 2：候选版本（Release Candidate）
委派给 **release-manager**：
- 从约定的提交切出发布分支
- 在所有相关文件中递增版本号
- 使用 `/release-checklist` 生成发布检查清单
- 冻结分支——不再接受功能变更，仅允许缺陷修复
- 输出：发布分支名称和检查清单

### 阶段 3：质量门禁（并行）
并行委派：
- **qa-lead**：执行完整回归测试套件。测试所有关键路径。确认不存在 S1/S2 级别缺陷。签收质量。
- **devops-engineer**：为所有目标平台构建发布产物。验证构建干净且可复现。在 CI（持续集成）中运行自动化测试。

### 阶段 4：本地化与性能
委派（如果资源允许，可与阶段 3 并行运行）：
- 验证所有字符串已完成翻译（如可用，委派给 localization-lead）
- 针对性能目标运行基准测试（如可用，委派给 performance-analyst）
- 输出：本地化和性能签收

### 阶段 5：放行/中止决策
委派给 **producer**：
- 收集以下人员的签收：qa-lead、release-manager、devops-engineer、technical-director
- 评估所有未解决的问题——是阻塞项还是可以随版本发布？
- 做出放行/中止决策
- 输出：附带理由的发布决策

### 阶段 6：部署（如通过）
委派给 **release-manager** + **devops-engineer**：
- 在版本控制中打标签
- 使用 `/changelog` 生成变更日志
- 部署到预发布环境进行最终冒烟测试
- 部署到生产环境
- 发布后监控 48 小时

### 阶段 7：发布后
- **release-manager**：生成发布报告（已发布内容、已推迟内容、度量数据）
- **producer**：更新里程碑跟踪，通知利益相关者
- **qa-lead**：监控接收到的缺陷报告，排查回归问题
- 如果出现问题，安排发布后复盘

## 输出
涵盖以下内容的总结报告：发布版本、范围、质量门禁结果、放行/中止决策、部署状态和监控计划。
