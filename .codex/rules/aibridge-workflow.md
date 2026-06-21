# AIBridge 操作规则

## 1. Python 路径

所有 bridge 命令必须使用 UE5 自带的 Python 解释器：

```bash
"$UE_ENGINE_DIR/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" .codex/skills/aibridge/bridge.py <命令>
```

系统 `python`/`python3` 在不同机器上可能不存在。`$UE_ENGINE_DIR` 在各机器的 `.codex/settings.local.json` 中配置。

## 2. 编辑器启停

- **启动**：用 Bash 的 `run_in_background` 启动 `.uproject`
- **关闭**：必须用 `bridge.py editor stop`，**禁止**使用 `pkill`、`kill`、`taskkill` 等系统命令
  - 原因：Windows 下 `pkill` 不可靠，杀不干净会导致编辑器进程残留
  - `bridge.py editor stop` 通过 Windows API 正确杀掉 UnrealEditor + LiveCodingConsole + TraceServer 整个进程树

## 3. 连接失败处理

`bridge.py ping` 失败时：

1. 先执行 `crash check` 检查是否有崩溃
2. 如果有崩溃 → `crash analyze` 分析原因
3. 如果没有崩溃 → 启动编辑器

## 4. 编辑器编译

优先使用 UBT 直接编译（不需要编辑器在线）：

```bash
"$UE_ENGINE_DIR/Engine/Build/BatchFiles/Build.bat" SekiroEditor Win64 Development -Project="F:/ProjectAI/Sekiro/Sekiro.uproject" -WaitMutex
```

需要编辑器在线时用 `bridge.py compile cpp`。