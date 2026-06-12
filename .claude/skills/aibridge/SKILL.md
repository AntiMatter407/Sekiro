---
name: aibridge
description: "通过 TCP JSON-RPC 操控 UE5 编辑器：查询状态、执行控制台命令、操作资产、运行 Python、编译 Blueprint、创建/修改 Blueprint/EnhancedInput/AnimBlueprint。"
argument-hint: "<命令> [参数...]"
user-invocable: true
allowed-tools: Bash, Read
---

# SekiroAIBridge — UE5 编辑器 AI 操控

通过 TCP JSON-RPC 连接运行中的 UE 编辑器，执行 9 类操作：
查询、控制台、资产、Python、编译、Blueprint、EnhancedInput、AnimBlueprint、PIE控制。

> **前提**：UE 编辑器运行中，SekiroAIBridge 插件已加载（监听 127.0.0.1:9877）。
> 用户可手动管理编辑器，也可请求 AI 通过 `editor start/stop/restart` 命令启停编辑器。

> **Windows Git Bash 用户**：MSYS2 会自动把 `/Game/...` 等 Unix 风格路径转为 Windows 路径（如 `C:/Program Files/Git/Game/...`），导致资产操作失败。所有 bridge.py 命令前需加 `MSYS2_ARG_CONV_EXCL='*'` 禁用此行为：
> ```bash
> MSYS2_ARG_CONV_EXCL='*' python .claude/skills/aibridge/bridge.py asset list /Game/
> ```

---

## 1. 连接检测

执行任何操作前，先检查桥接是否可达：

```bash
python3 .claude/skills/aibridge/bridge.py ping
```

如果返回 `连接被拒绝`：提示用户"请确认 UE 编辑器已启动并加载 SekiroAIBridge 插件（监听 127.0.0.1:9877）"，等待用户确认后重试。

---

## 2. 命令参考

所有命令通过 `bridge.py` 执行：

### 编辑器状态查询

```bash
python3 .claude/skills/aibridge/bridge.py query project     # 项目名称、引擎版本
python3 .claude/skills/aibridge/bridge.py query level       # 当前关卡
python3 .claude/skills/aibridge/bridge.py query selection   # 选中的 Actor 列表
python3 .claude/skills/aibridge/bridge.py query all         # 全部信息
```

### 控制台命令

```bash
python3 .claude/skills/aibridge/bridge.py console "stat fps"
python3 .claude/skills/aibridge/bridge.py console "obj list"
python3 .claude/skills/aibridge/bridge.py console "memreport"
```

### 资产操作

```bash
python3 .claude/skills/aibridge/bridge.py asset list /Game/
python3 .claude/skills/aibridge/bridge.py asset info /Game/BP_Player
python3 .claude/skills/aibridge/bridge.py asset create /Game/Data/DT_Config DataTable
python3 .claude/skills/aibridge/bridge.py asset import_file /Game/Textures/T_MyTex "F:/path/to/texture.png"
python3 .claude/skills/aibridge/bridge.py asset delete /Game/Temp/ToDelete
python3 .claude/skills/aibridge/bridge.py asset rename /Game/Old /Game/New
```

`import_file` imports a single file (PNG/TGA/BMP/DDS) as a Texture2D asset. The source path must be an absolute filesystem path. This is the recommended way to import textures — Python's `AssetImportTask` hangs when called via bridge (GameThread deadlock).

### Python 执行

```bash
python3 .claude/skills/aibridge/bridge.py python "import unreal; print(unreal.get_editor_subsystem(...))"
python3 .claude/skills/aibridge/bridge.py python --file "F:/path/to/script.py"
python3 .claude/skills/aibridge/bridge.py python --file "F:/path/to/script.py" --args "/Game/config.json"
```

`--file` executes a Python file. `--args` passes arguments visible to the script via `sys.argv[1:]`.

### 编译

```bash
python3 .claude/skills/aibridge/bridge.py compile cpp          # C++ 编译（直接调UBT，返回结构化错误）
python3 .claude/skills/aibridge/bridge.py compile all          # 编译所有 BP（通过编辑器）
python3 .claude/skills/aibridge/bridge.py compile changed      # 仅编译已修改的 BP
python3 .claude/skills/aibridge/bridge.py compile selected     # 仅编译选中的 BP
```

**compile cpp 返回格式**：
```json
{
  "success": false,
  "exitCode": 6,
  "errors": [
    {"file": "D:\\Sekiro\\...\\Foo.cpp", "line": 42, "code": "C2679", "message": "binary '=': no operator...", "fatal": false}
  ],
  "warnings": [...],
  "errorCount": 1,
  "warningCount": 0,
  "summary": "最后5行UBT输出"
}
```

### Blueprint 操作

```bash
python3 .claude/skills/aibridge/bridge.py blueprint create /Game/BP_MyActor Actor
python3 .claude/skills/aibridge/bridge.py blueprint addvar /Game/BP_MyActor Health float
python3 .claude/skills/aibridge/bridge.py blueprint addfunc /Game/BP_MyActor OnDamage
python3 .claude/skills/aibridge/bridge.py blueprint addnode /Game/BP_MyActor OnDamage PrintString
python3 .claude/skills/aibridge/bridge.py blueprint compile /Game/BP_MyActor
```

### Enhanced Input 操作

创建和配置 Enhanced Input 系统的资产：InputAction、InputMappingContext、按键映射。

```bash
python3 .claude/skills/aibridge/bridge.py enhanced_input create_action /Game/Input/IA_Jump axis1d
python3 .claude/skills/aibridge/bridge.py enhanced_input create_context /Game/Input/IMC_Default
python3 .claude/skills/aibridge/bridge.py enhanced_input map_key /Game/Input/IMC_Default /Game/Input/IA_Jump SpaceBar
python3 .claude/skills/aibridge/bridge.py enhanced_input unmap_key /Game/Input/IMC_Default /Game/Input/IA_Jump SpaceBar
python3 .claude/skills/aibridge/bridge.py enhanced_input info /Game/Input/IA_Jump
```

**create_action 参数**: value_type 可选 `bool`(默认) / `axis1d` / `axis2d` / `axis3d`
**map_key 参数**: `<IMC路径> <IA路径> <按键名>`，按键名如 `SpaceBar` / `LeftMouseButton` / `W` 等

### Animation Blueprint 操作

创建动画蓝图、管理状态机（状态/转换）、添加动画节点。

```bash
python3 .claude/skills/aibridge/bridge.py anim_blueprint create /Game/Anim/ABP_Character /Game/Anim/SK_Character
python3 .claude/skills/aibridge/bridge.py anim_blueprint add_state /Game/Anim/ABP_Character Idle
python3 .claude/skills/aibridge/bridge.py anim_blueprint add_state /Game/Anim/ABP_Character Run
python3 .claude/skills/aibridge/bridge.py anim_blueprint add_transition /Game/Anim/ABP_Character Idle Run 0.15 cubic
python3 .claude/skills/aibridge/bridge.py anim_blueprint add_node /Game/Anim/ABP_Character Idle sequence_player /Game/Anim/A_Idle
python3 .claude/skills/aibridge/bridge.py anim_blueprint info /Game/Anim/ABP_Character
python3 .claude/skills/aibridge/bridge.py anim_blueprint compile /Game/Anim/ABP_Character
```

**create 参数**: `<ABP路径> <骨架路径>` — 骨架必须是已存在的 USkeleton 资产
**add_node 参数**: `<ABP路径> <状态名> <节点类型> <动画资产路径>`
  - 节点类型: `sequence_player` (需 AnimSequence) / `blend_space_player` (需 BlendSpace)
**add_transition 参数**: `<ABP路径> <源状态> <目标状态> [crossfade_duration] [blend_mode]`
  - blend_mode 可选: linear / cubic / sinusoidal / cubic_in_out 等

### PIE 控制

控制 Play In Editor 会话的启动、停止、暂停、恢复，支持多种运行模式和网络配置。

```bash
python3 .claude/skills/aibridge/bridge.py pie status                # 查询 PIE 状态
python3 .claude/skills/aibridge/bridge.py pie start                 # 启动 PIE（默认：视口内）
python3 .claude/skills/aibridge/bridge.py pie start standalone      # 独立进程
python3 .claude/skills/aibridge/bridge.py pie start mobile          # 移动端预览
python3 .claude/skills/aibridge/bridge.py pie start vulkan          # Vulkan 预览
python3 .claude/skills/aibridge/bridge.py pie start vr              # VR 预览
python3 .claude/skills/aibridge/bridge.py pie start simulate        # 模拟模式（无玩家）
python3 .claude/skills/aibridge/bridge.py pie start --clients 2 --listen  # 2 客户端 ListenServer
python3 .claude/skills/aibridge/bridge.py pie start --clients 4 --dedicated  # 4 客户端 DedicatedServer
python3 .claude/skills/aibridge/bridge.py pie stop                  # 停止 PIE
python3 .claude/skills/aibridge/bridge.py pie pause                 # 暂停 PIE
python3 .claude/skills/aibridge/bridge.py pie resume                # 恢复 PIE
python3 .claude/skills/aibridge/bridge.py pie late_join             # 添加一个客户端
```

**start 参数说明**:
- 位置参数 `mode`: selected（默认）/ standalone / mobile / vulkan / vr / simulate
- `--clients N`: 客户端数量（默认 1）
- `--net_mode standalone|dedicated|listen`: 网络模式
- `--dedicated`: DedicatedServer 快捷方式
- `--listen`: ListenServer 快捷方式
- `--server`: 等效 --listen
- `--viewport`: 视口内运行（默认 true）

### 工具列表

```bash
python3 .claude/skills/aibridge/bridge.py tools
```

### 编辑器生命周期

用户可请求 AI 管理编辑器启停。这些操作无需通过 TCP（直接操作系统进程）。

```bash
python3 .claude/skills/aibridge/bridge.py editor start     # 启动编辑器（自动清理 cmd 窗口）
python3 .claude/skills/aibridge/bridge.py editor stop      # 关闭编辑器 + LiveCoding/Trace 子进程
python3 .claude/skills/aibridge/bridge.py editor restart   # 重启编辑器
```

**启动流程**：`DETACHED_PROCESS` 无 cmd 窗口 → 等待 TCP 9877 就绪（最多 90 秒）。
**关闭流程**：杀 UnrealEditor + LiveCodingConsole + TraceServer + CEFSubProcess → 确认进程已退出。
**重启流程**：stop → 等 2 秒 → start。

> 这些操作有副作用，执行前需用户确认。

### 崩溃追踪

编辑器运行时崩溃后，检查并分析崩溃原因，定位源码，自动修复。

```bash
python3 .claude/skills/aibridge/bridge.py crash check      # 检测最近是否崩溃
python3 .claude/skills/aibridge/bridge.py crash analyze    # 分析崩溃：错误信息、调用栈、源码定位
python3 .claude/skills/aibridge/bridge.py crash list       # 列出历史崩溃
```

**分析来源**：
- `Saved/Crashes/UECC-*/CrashContext.runtime-xml` — 错误类型、调用栈模块
- `Saved/Crashes/UECC-*/*.log` / `Saved/Logs/Sekiro.log` — Fatal/Assert 日志行
- 自动提取项目源码文件路径和行号（`sourceHints`）

---

## 3. 安全规则

以下操作具有副作用，执行前必须向用户确认：

| 操作 | 风险 |
|------|------|
| `asset delete` | 不可逆删除资产 |
| `asset create` | 创建新资产 |
| `compile all` | 可能触发大量编译 |
| `blueprint create` | 创建新 Blueprint |
| `blueprint addvar/addfunc/addnode` | 修改 Blueprint 结构 |
| `enhanced_input map_key/unmap_key` | 修改输入映射配置 |
| `anim_blueprint add_state/add_transition/add_node` | 修改动画蓝图图结构 |
| `pie start` | 启动 PIE 会话 |
| `console` 带写入命令 | 可能修改编辑器状态 |

**确认格式**：
> "即将执行 `[命令]`。这可能 [影响说明]。是否继续？"

只读操作（`query`、`asset list`、`asset info`、`ping`、`tools`、`console "stat*"`、`pie status`）无需确认。

---

## 4. 错误处理

| 错误 | 处理 |
|------|------|
| 连接被拒绝 | 提示用户确认编辑器是否已启动并加载插件 |
| 连接超时 | 重试 1 次后报告失败 |
| 工具未注册 | 检查插件是否正确加载 |
| 缺少参数 | 展示该命令的正确用法 |
| 只读模式 | 提示在项目设置中关闭只读模式 |
| 用户拒绝 | 终止操作并报告 |

---

## 5. 工作流示例

### 示例：C++ 编译 + 自动修复

```
用户: /aibridge compile cpp
  → 编辑器在线 → Live Coding 模式，编辑器离线 → 直接 UBT
  → 触发编译 → 轮询 UBT 日志 → 解析错误
  → 返回错误列表（file/line/code/message）

  → AI 自动：
    1. 读取错误文件 → 分析 → 修复（Edit）
    2. 重新 compile cpp → 再次编译
    3. 成功 → 报告"编译通过，0 errors"
    4. 仍失败 → 重复修复（最多3轮）→ 超过则请求用户介入
```

**规则**：
- 最多自动修复 3 轮，超过则停止并请求用户介入
- 每轮修复后必须重新编译验证
- 修复范围仅限于本次修改涉及的文件

### 示例：运行时崩溃追踪 + 修复

```
用户: /aibridge crash check
  → 检测到 3分钟前有一次崩溃（UECC-Windows-xxx）

用户: /aibridge crash analyze
  → 读取 CrashContext.runtime-xml + 日志文件
  → 返回：
    errorMessage: "Access violation - code c0000005"
    sourceHints: [{raw: "SekiroImport/SekiroModelParser.cpp", line: 412}]
    logErrors: ["Fatal error: [File:.../SekiroModelParser.cpp] [Line: 412] ..."]

  → AI 自动：
    1. Read SekiroModelParser.cpp 第412行
    2. 分析原因：空指针解引用
    3. 修复（加空指针检查）
    4. compile cpp → 通过
    5. 提示用户重启编辑器验证
```

### 示例：创建 DataTable 资产

```
用户: /aibridge asset create /Game/Data/DT_Enemies DataTable
→ AI 检查连接 → 请求确认 → 执行 → 报告结果
```

### 示例：查询编辑器状态后编译

```
用户: /aibridge query project
→ 输出项目名、引擎版本

用户: /aibridge compile changed
→ AI 请求确认 → 执行 → 输出编译结果
```
