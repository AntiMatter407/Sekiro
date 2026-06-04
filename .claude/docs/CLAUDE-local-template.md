# CLAUDE.local.md 模板

将此文件复制到项目根目录，命名为 `CLAUDE.local.md`，用于个人偏好覆盖。
此文件已被 Git 忽略（gitignored），不会被提交。

```markdown
# 个人偏好

## 模型偏好
- 复杂设计任务优先使用 Opus
- 快速查找和简单编辑使用 Haiku

## 工作流偏好
- 代码变更后始终运行测试
- 上下文使用率达到 60% 时主动压缩（Compact）
- 无关任务之间使用 /clear

## 本地环境
- Python 命令: python（或 py / python3）
- Shell: Windows 上的 Git Bash
- IDE: 安装了 Claude Code 扩展的 VS Code

## 沟通风格
- 保持回复简洁
- 所有代码引用中显示文件路径
- 简要说明架构决策

## 个人快捷指令
- 当我说 "review" 时，对最近变更的文件运行 /code-review
- 当我说 "status" 时，显示 git 状态 + Sprint 进度
```

## 设置

1. 将此模板复制到项目根目录：`cp .claude/docs/CLAUDE-local-template.md CLAUDE.local.md`
2. 编辑为你的偏好
3. 确认 `CLAUDE.local.md` 在 `.gitignore` 中（Claude Code 从项目根目录读取它）
