# Hook 输入/输出模式（Schema）

本文档记录每个 Claude Code Hook（钩子）在各事件类型中通过标准输入（stdin）接收的 JSON 载荷。

## PreToolUse

在工具执行前触发。可以 **允许**（exit 0）或 **阻止**（exit 2）。

### PreToolUse: Bash

```json
{
  "tool_name": "Bash",
  "tool_input": {
    "command": "git commit -m 'feat: add player health system'",
    "description": "Commit changes with message",
    "timeout": 120000
  }
}
```

### PreToolUse: Write

```json
{
  "tool_name": "Write",
  "tool_input": {
    "file_path": "src/gameplay/health.gd",
    "content": "extends Node\n..."
  }
}
```

### PreToolUse: Edit

```json
{
  "tool_name": "Edit",
  "tool_input": {
    "file_path": "src/gameplay/health.gd",
    "old_string": "var health = 100",
    "new_string": "var health: int = 100"
  }
}
```

### PreToolUse: Read

```json
{
  "tool_name": "Read",
  "tool_input": {
    "file_path": "src/gameplay/health.gd"
  }
}
```

## PostToolUse

在工具完成后触发。**无法阻止**（阻止相关的退出码被忽略）。标准错误（stderr）消息会以警告形式显示。

### PostToolUse: Write

```json
{
  "tool_name": "Write",
  "tool_input": {
    "file_path": "assets/data/enemy_stats.json",
    "content": "{\"goblin\": {\"health\": 50}}"
  },
  "tool_output": "File written successfully"
}
```

### PostToolUse: Edit

```json
{
  "tool_name": "Edit",
  "tool_input": {
    "file_path": "assets/data/enemy_stats.json",
    "old_string": "\"health\": 50",
    "new_string": "\"health\": 75"
  },
  "tool_output": "File edited successfully"
}
```

## SubagentStart

通过 Task 工具生成子代理（Subagent）时触发。

```json
{
  "agent_name": "game-designer",
  "model": "sonnet",
  "description": "Design the combat healing mechanic"
}
```

## SessionStart

在 Claude Code 会话开始时触发。**无标准输入**——Hook 直接运行，其标准输出会作为上下文显示给 Claude。

## PreCompact

在上下文窗口压缩前触发。**无标准输入**——Hook 运行以在压缩发生前保存状态。

## Stop

在 Claude Code 会话结束时触发。**无标准输入**——Hook 运行用于清理和日志记录。

## 退出码参考

| 退出码 | 含义 | 适用事件 |
|--------|------|----------|
| 0 | 允许 / 成功 | 所有事件 |
| 2 | 阻止（stderr 显示给 Claude） | 仅 PreToolUse |
| 其他 | 视为错误，工具继续执行 | 所有事件 |

## 注意事项

- Hook 通过 **stdin**（管道）接收 JSON。使用 `INPUT=$(cat)` 来捕获。
- 如可用 `jq` 进行解析，否则回退到 `grep` 以保证跨平台兼容性。
- 在 Windows 上，`grep -P`（Perl 正则表达式）通常不可用。请改用 `grep -E`（POSIX 扩展正则）。
- Windows 上的路径分隔符可能是 `\`。比较路径时使用 `sed 's|\\|/|g'` 进行标准化。
