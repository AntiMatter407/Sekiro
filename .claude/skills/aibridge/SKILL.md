---
name: aibridge
description: "通过 TCP JSON-RPC 操控 UE5 编辑器：查询状态、执行控制台命令、操作资产、运行 Python、编译 C++/Blueprint、创建/修改 Blueprint/EnhancedInput/AnimBlueprint、PIE 控制与输入模拟、启动/停止编辑器、崩溃分析。"
argument-hint: "<命令> [参数...]"
user-invocable: true
allowed-tools: Bash, Read
---

# SekiroAIBridge — UE5 编辑器 AI 操控

通过 TCP JSON-RPC 连接运行中的 UE 编辑器，执行 12 类操作。
bridge.py 位于 `Script/aibridge/bridge.py`（与 Codex 共享同一份）。

> **完整参考命令速查和参数说明**：`Docs/aibridge-reference.md`
> 所有命令详情、安全规则、工作流示例、错误处理均可查阅此文档。

> **前提**：UE 编辑器运行中，SekiroAIBridge 插件已加载（监听 127.0.0.1:9877）。
> 启动方式：用户手动启动，或 `editor start` 由 AI 自动启动。

> **跨平台注意事项**：
> 1. 所有 bridge 命令必须使用 **UE5 自带的 Python 解释器**：
>    `$UE_ENGINE_DIR/Engine/Binaries/ThirdParty/Python3/Win64/python.exe`
> 2. `$UE_ENGINE_DIR` 在各机器的 `.claude/settings.local.json` 中配置
> 3. **MSYS2_ARG_CONV_EXCL 规则**：执行时 **必须** 在 Python 解释器前加
>    `MSYS2_ARG_CONV_EXCL='*'`，防止 Git Bash 将 `/Game/...` 转为 Windows 路径
>    这是硬性规则，每一条 bridge 命令都要遵守，不可省略

---

## 1. 连接检测

```bash
MSYS2_ARG_CONV_EXCL='*' "$UE_ENGINE_DIR/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" Script/aibridge/bridge.py ping
```

返回 `pong` 表示就绪。返回 `连接被拒绝`：
1. 先 `crash check` 检查崩溃
2. 有崩溃 → `crash analyze` 分析
3. 无崩溃 → `editor start` 启动编辑器

---

## 2. 命令速查

完整参数说明见 `Docs/aibridge-reference.md`。

### 查询
```bash
bridge.py query project|level|selection|all
```

### 控制台
```bash
bridge.py console "stat fps"|"obj list"|"memreport"
```

### 资产操作
```bash
bridge.py asset list|info|create|delete|rename|import_file <参数>
```

### Python 执行
```bash
bridge.py python "单行代码"
bridge.py python --file "路径/script.py" [--args "参数"]
```

### 编译
```bash
bridge.py compile cpp|all|changed|selected
```
`compile cpp` 返回结构化 JSON（errors/warnings/exitCode），AI 可自动修复后重新编译，最多 3 轮。

### Blueprint
```bash
bridge.py blueprint create|addvar|addfunc|addnode|compile|layout <参数>
```

### Enhanced Input
```bash
bridge.py enhanced_input create_action|create_context|info|map_key|unmap_key|configure_triggers <参数>
```

### Animation Blueprint
```bash
bridge.py anim_blueprint create|add_state|add_transition|delete_transition|add_node|rename_node
bridge.py anim_blueprint info|compile|layout|set_anim_class <参数>
```

### PIE
```bash
bridge.py pie status|start|stop|pause|resume|late_join [--clients N] [--listen] [--dedicated]
```

### 输入模拟（PIE 运行时）
```bash
bridge.py input_simulate <action> [--x <值>] [--y <值>] [--hold <秒>] [--delay <秒>]
```
action: attack/guard/dodge/jump/interact/use_item/healing_gourd/grapple/prosthetic/lock_on/crouch/move/look/cycle_item_next|prev/pause/menu

### 编辑器生命周期
```bash
bridge.py editor start|stop|restart
```

### 崩溃追踪
```bash
bridge.py crash check|analyze|list
```

---

## 3. 安全规则

| 操作 | 确认 |
|------|------|
| `asset delete/create` | ✅ |
| `compile all` | ✅ |
| `blueprint create/addvar/addfunc/addnode/layout` | ✅ |
| `enhanced_input map_key/unmap_key/configure_triggers` | ✅ |
| `anim_blueprint add/delete/set/rename` 系列 | ✅ |
| `pie start` | ✅ |
| `editor start/stop/restart` | ✅ |
| 带写入的 `console` | ✅ |
| `input_simulate` | ✅ |
| 只读（query/asset list&info/ping/tools/pie status/crash check） | ❌ 无需确认 |
