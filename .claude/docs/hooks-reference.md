# 活跃钩子 (Active Hooks)

钩子 (Hooks) 在 `.claude/settings.json` 中配置，会自动触发：

| 钩子 (Hook) | 事件 (Event) | 触发条件 (Trigger) | 操作 (Action) |
| ---- | ----- | ------- | ------ |
| `validate-commit.sh` | PreToolUse (Bash) | `git commit` 命令 | 验证设计文档章节、JSON 数据文件、硬编码值、TODO 格式 |
| `validate-push.sh` | PreToolUse (Bash) | `git push` 命令 | 向受保护分支 (develop/main) 推送时发出警告 |
| `validate-assets.sh` | PostToolUse (Write/Edit) | 资产文件变更 | 检查 `assets/` 目录下文件的命名规范和 JSON 有效性 |
| `session-start.sh` | SessionStart | 会话开始 | 加载 Sprint 上下文、里程碑、Git 活动；检测并预览活跃会话状态文件以便恢复 |
| `detect-gaps.sh` | SessionStart | 会话开始 | 检测新项目（建议运行 /start）以及代码/原型存在时的文档缺失，建议运行 /reverse-document 或 /project-stage-detect |
| `pre-compact.sh` | PreCompact | 上下文压缩 | 在压缩前将会话状态（active.md、已修改文件、进行中的设计文档）转储到对话中，使其在摘要后仍可保留 |
| `session-stop.sh` | Stop | 会话结束 | 总结成果并更新会话日志 |
| `log-agent.sh` | SubagentStart | 子代理启动 | 记录所有子代理调用的审计追踪，包含时间戳 |

钩子参考文档：`.claude/docs/hooks-reference/`
钩子输入 Schema 文档：`.claude/docs/hooks-reference/hook-input-schemas.md`
