# AIBridge 操作规则

默认只编辑简单文档和代码，不调用编辑器查询、启停、编译、资产生成或 PIE。所需操作先说明为下一最小步骤；用户具体要求相应操作，或在该步骤已经说明后单独回复“继续”，均视为对应步骤的明确授权，但不能由此推导出后续整套编辑器操作授权。

当用户已经明确授权当前脚本化操作时，由 Agent 负责通过 AIBridge 编写并执行脚本、读取结构化结果和处理必要重试，不再把可直接执行的脚本命令转交用户。该授权严格限制在当前脚本的职责内；“继续”只覆盖上一条回复明确列出的下一最小步骤及其必要操作，不附带授权后续 C++/Blueprint 编译、PIE、输入模拟或编辑器启停。

脚本运行暴露出项目或通用插件缺陷时，应同步修复插件源码，不能以项目脚本兜底跳过根因。若后续操作依赖新的 C++ 二进制，源码修改完成后必须暂停，请用户编译或明确授权 Agent 编译；源码修改本身不扩大为编译授权。

## 1. Python 路径

所有 bridge 命令必须使用 UE5 自带的 Python 解释器，bridge.py 位于 `Script/aibridge/bridge.py`：

```bash
MSYS2_ARG_CONV_EXCL='*' "$UE_ENGINE_DIR/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" Script/aibridge/bridge.py <命令>
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
3. 如果没有崩溃 → 列出启动编辑器待办；仅在用户具体要求启动编辑器，或用“继续”授权已说明的编辑器启动步骤时执行

## 4. 编辑器编译授权

默认不执行 C++、Blueprint 或 AnimBlueprint 编译，也不因源码修改自动启动编辑器。只有用户明确要求编译，或用“继续”授权上一条回复已说明的编译步骤时，才使用下列方式执行其指定范围。

优先使用 UBT 直接编译（不需要编辑器在线）：

```bash
"$UE_ENGINE_DIR/Engine/Build/BatchFiles/Build.bat" SekiroEditor Win64 Development -Project="F:/ProjectAI/Sekiro5.8/Sekiro.uproject" -WaitMutex
```

需要编辑器在线时用 `bridge.py compile cpp`。
