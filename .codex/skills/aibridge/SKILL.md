---
name: aibridge
description: "通过 TCP JSON-RPC 操控 UE5 编辑器：查询状态、执行控制台命令、操作资产、运行 Python、编译 C++/Blueprint、创建/修改 Blueprint/EnhancedInput/AnimBlueprint、PIE 控制与输入模拟、启动/停止编辑器、崩溃分析。"
---

# SekiroAIBridge — UE5 编辑器 AI 操控

通过 TCP JSON-RPC 连接运行中的 UE 编辑器，执行 12 类操作。
bridge.py 位于 `Script/aibridge/bridge.py`（与 Claude 共享同一份）。

> **完整参考**：`Docs/aibridge-reference.md`（所有命令参数、安全规则、工作流示例）

> **前提**：UE 编辑器运行中，SekiroAIBridge 插件已加载（监听 127.0.0.1:9877）

> **跨平台注意事项**：
> 1. 使用 UE5 自带 Python：`$UE_ENGINE_DIR/Engine/Binaries/ThirdParty/Python3/Win64/python.exe`
> 2. `$UE_ENGINE_DIR` 在 `.codex/settings.local.json` 的 `env.UE_ENGINE_DIR`
> 3. **MSYS2_ARG_CONV_EXCL='*'** 必须在 Python 前加，防止路径自动转换

---

## 命令速查

完整参数说明见 `Docs/aibridge-reference.md`。

### 连接检测
```bash
MSYS2_ARG_CONV_EXCL='*' "$UE_ENGINE_DIR/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" Script/aibridge/bridge.py ping
```

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
bridge.py python "代码" | --file "脚本.py" [--args "参数"]
```

### 编译
```bash
bridge.py compile cpp|all|changed|selected
```

### Blueprint
```bash
bridge.py blueprint create|addvar|addfunc|addnode|compile|layout <参数>
```

### Enhanced Input
```bash
bridge.py enhanced_input create_action|create_context|map_key|unmap_key|info|configure_triggers <参数>
```

### Animation Blueprint
```bash
bridge.py anim_blueprint create|add_state|add_transition|delete_transition|add_node|rename_node <参数>
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

### 编辑器生命周期
```bash
bridge.py editor start|stop|restart
```

### 崩溃追踪
```bash
bridge.py crash check|analyze|list
```

---

## 安全规则

| 操作 | 确认 |
|------|------|
| asset delete/create | ✅ |
| compile all | ✅ |
| blueprint addvar/addfunc/addnode | ✅ |
| enhanced_input map_key/unmap_key/configure_triggers | ✅ |
| anim_blueprint add_state/add_transition/add_node/delete_transition/rename_node/set_anim_class | ✅ |
| pie start | ✅ |
| editor start/stop/restart | ✅ |
| console 带写入 | ✅ |
| input_simulate | ✅ |
| 只读（query/asset list&info/ping/tools/pie status/crash check） | ❌ 无需确认 |
