# settings.local.json 模板

创建 `.claude/settings.local.json` 用于个人覆写配置，这些配置**不应**提交到版本控制中。将其添加到 `.gitignore`。

## settings.local.json 示例

```json
{
  "permissions": {
    "allow": [
      "Bash(git *)",
      "Bash(npm *)",
      "Read",
      "Glob",
      "Grep"
    ],
    "deny": [
      "Bash(rm -rf *)",
      "Bash(git push --force *)"
    ]
  }
}
```

## 权限模式（Permission Modes）

Claude Code 支持不同的权限模式。游戏开发推荐配置：

### 开发期间（默认）
使用**普通模式（normal mode）** — Claude 在执行大多数命令前会询问确认。这对于生产代码来说最安全。

### 原型开发期间
使用**自动接受模式（auto-accept mode）** 并限制作用范围 — 在可丢弃代码上加快迭代速度。仅在 `prototypes/` 目录中工作时使用此模式。

### 代码审查期间
使用**只读权限（read-only）** — Claude 可以读取和搜索文件，但不能修改。

## 本地自定义钩子（Hooks）

你可以在 `settings.local.json` 中添加个人钩子来扩展（而非覆盖）项目的钩子。例如，在构建完成时添加通知：

```json
{
  "hooks": {
    "Stop": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "bash -c 'echo Session ended at $(date)'",
            "timeout": 5
          }
        ]
      }
    ]
  }
}
```
