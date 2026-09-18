# SekiroAIBridge — UE5 编辑器 AI 操控参考文档

> 通过 TCP JSON-RPC 连接运行中的 UE 编辑器，执行 9 类操作：查询、控制台、资产、Python、编译、Blueprint、EnhancedInput、AnimBlueprint、PIE控制、输入模拟、崩溃分析、编辑器生命周期。

## 前提

- UE 编辑器运行中，SekiroAIBridge 插件已加载（监听 127.0.0.1:9877）
- 用户可手动管理编辑器，也可通过 `editor start/stop/restart` 命令启停

## 跨平台注意事项

1. 所有 bridge 命令必须使用 **UE5 自带的 Python 解释器**：
   `$UE_ENGINE_DIR/Engine/Binaries/ThirdParty/Python3/Win64/python.exe`
2. `$UE_ENGINE_DIR` 在各机器的 `.claude/settings.local.json` 中配置
3. **MSYS2_ARG_CONV_EXCL 规则**：所有命令执行时 **必须** 在 Python 解释器前加 `MSYS2_ARG_CONV_EXCL='*'`，防止 Git Bash (MSYS2) 自动将 `/Game/...` 等 Unix 路径转为 Windows 路径——**硬性规则，不可省略**

---

## 1. 连接检测

```bash
"$UE_ENGINE_DIR/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" .claude/skills/aibridge/bridge.py ping
```

返回 `pong` 表示就绪。返回 `连接被拒绝`：
1. 先执行 `crash check` 检查崩溃
2. 有崩溃 → `crash analyze` 分析原因
3. 无崩溃 → 启动编辑器

---

## 2. 命令参考

### 2.1 查询（只读）

```bash
bridge.py query project      # 项目名称、引擎版本
bridge.py query level        # 当前关卡
bridge.py query selection    # 选中的 Actor 列表
bridge.py query all          # 全部信息
```

### 2.2 控制台

```bash
bridge.py console "stat fps"
bridge.py console "obj list"
bridge.py console "memreport"
```

### 2.3 资产操作

```bash
bridge.py asset list /Game/                           # 列出资产
bridge.py asset info /Game/Path/Asset                 # 资产详情
bridge.py asset create /Game/Path/Name DataTable      # 创建资产（类型: DataTable/Blueprint等）
bridge.py asset import_file /Game/Path/T_Name "D:/tex.png"  # 导入纹理（PNG/TGA/BMP/DDS）
bridge.py asset delete /Game/Path/ToDelete            # 删除资产（不可逆）
bridge.py asset rename /Game/Old /Game/New            # 重命名资产
```

**`import_file` 说明**：导入单个文件作为 Texture2D。源路径需是绝对路径。推荐方式——Python 的 `AssetImportTask` 在桥接中会死锁。

### 2.4 Python 执行

```bash
bridge.py python "import unreal; print(unreal.EditorLevelLibrary.get_all_level_actors())"  # 单行
bridge.py python --file "D:/Scripts/my_script.py"                                           # 执行文件
bridge.py python --file "D:/Scripts/my_script.py" --args "/Game/config.json"                # 传参（在 sys.argv[1:] 中）
```

**`--file` 限制**：仅支持 1 个文件，不支持多文件。参数通过 `--args` 传递。

### 2.5 编译

```bash
bridge.py compile cpp         # C++ 编译（编辑器在线→Live Coding；离线→直接 UBT）
bridge.py compile all         # 编译所有 Blueprint
bridge.py compile changed     # 仅编译已修改的 BP
bridge.py compile selected    # 仅编译选中的 BP
```

**`compile cpp` 返回格式**：
```json
{
  "success": false,
  "exitCode": 6,
  "errors": [{"file": "path/Foo.cpp", "line": 42, "code": "C2679", "message": "...", "fatal": false}],
  "warnings": [],
  "errorCount": 1,
  "warningCount": 0,
  "summary": "最后5行UBT输出"
}
```

### 2.6 Blueprint 操作

```bash
bridge.py blueprint create /Game/BP_MyActor Actor           # 创建 Blueprint（父类默认 Pawn）
bridge.py blueprint create /Game/BP_MyActor Character        # 创建 Character 子类
bridge.py blueprint addvar /Game/BP_MyActor Health float     # 添加变量
bridge.py blueprint addfunc /Game/BP_MyActor OnDamage       # 添加自定义函数
bridge.py blueprint addnode /Game/BP_MyActor OnDamage PrintString  # 添加K2节点
bridge.py blueprint compile /Game/BP_MyActor                 # 编译 Blueprint
bridge.py blueprint layout /Game/BP_MyActor                  # 自动排版图中所有节点
bridge.py blueprint layout /Game/BP_MyActor MyGraph          # 排版指定图
```

### 2.7 Enhanced Input 操作

```bash
bridge.py enhanced_input create_action /Game/Input/IA_Jump axis1d    # 创建 InputAction
bridge.py enhanced_input create_action /Game/Input/IA_Fire bool      # 创建布尔型 InputAction
bridge.py enhanced_input create_context /Game/Input/IMC_Default      # 创建 InputMappingContext
bridge.py enhanced_input map_key /Game/Input/IMC_Default /Game/Input/IA_Jump SpaceBar     # 绑定按键
bridge.py enhanced_input unmap_key /Game/Input/IMC_Default /Game/Input/IA_Jump SpaceBar   # 移除按键
bridge.py enhanced_input info /Game/Input/IA_Jump                    # 查询资产详情
bridge.py enhanced_input configure_triggers /Game/Input/IA_Attack Down:0.35                # 配置触发器：按下一段时间后触发
bridge.py enhanced_input configure_triggers /Game/Input/IA_Jump Immediate,DoubleTap:0.3    # 多触发器
```

**`create_action` value_type**: `bool`(默认) / `axis1d` / `axis2d` / `axis3d`
**`configure_triggers` 格式**: `TriggerName:Arg1:Arg2,...`（冒号分隔参数，逗号分隔多触发器）

### 2.8 Animation Blueprint 操作

```bash
# 创建
bridge.py anim_blueprint create /Game/Anim/ABP_Character /Game/Anim/SK_Skeleton   # 必须指定已存在的 USkeleton

# 状态管理
bridge.py anim_blueprint add_state /Game/Anim/ABP_Character Idle            # 添加状态
bridge.py anim_blueprint rename_node /Game/Anim/ABP_Character state 旧名 新名  # 重命名状态
bridge.py anim_blueprint rename_node /Game/Anim/ABP_Character state_machine 新名  # 重命名状态机

# 转换管理
bridge.py anim_blueprint add_transition /Game/Anim/ABP_Character Idle Run 0.15 cubic     # 添加转换（带过渡参数）
bridge.py anim_blueprint add_transition /Game/Anim/ABP_Character Idle Run --auto-rule    # 自动创建转换规则
bridge.py anim_blueprint add_transition /Game/Anim/ABP_Character Idle Run --condition "bool:IsMoving"        # bool 条件
bridge.py anim_blueprint add_transition /Game/Anim/ABP_Character Idle Run --condition "not_bool:IsDead"     # 非条件
bridge.py anim_blueprint add_transition /Game/Anim/ABP_Character Idle Run --condition "float_compare:Speed:Greater:0.1"  # 浮点比较
bridge.py anim_blueprint add_transition /Game/Anim/ABP_Character Idle Run --condition "time_remaining:0.5"   # 剩余时间
bridge.py anim_blueprint add_transition /Game/Anim/ABP_Character Idle Run --bidirectional  # 双向转换
bridge.py anim_blueprint delete_transition /Game/Anim/ABP_Character Idle Run   # 删除转换

# 节点管理
bridge.py anim_blueprint add_node /Game/Anim/ABP_Character Idle sequence_player /Game/Anim/A_Idle         # Sequence 节点
bridge.py anim_blueprint add_node /Game/Anim/ABP_Character Idle sequence_player /Game/Anim/A_Idle --loop  # 循环
bridge.py anim_blueprint add_node /Game/Anim/ABP_Character Idle blend_space_player /Game/Anim/BS_Walk     # BlendSpace 节点
bridge.py anim_blueprint add_node /Game/Anim/ABP_Character Idle sequence_player /Game/Anim/A_Idle --pin-x Speed --pin-y Angle  # 连接变量到引脚

# 查询与编译
bridge.py anim_blueprint info /Game/Anim/ABP_Character      # 查询结构
bridge.py anim_blueprint compile /Game/Anim/ABP_Character   # 编译
bridge.py anim_blueprint layout /Game/Anim/ABP_Character    # 自动排版状态机节点

# 设置动画蓝图到角色
bridge.py anim_blueprint set_anim_class /Game/BP_MyCharacter --anim_bp /Game/Anim/ABP_Character  # 将ABP赋给角色Mesh组件
```

**`add_transition` 参数**:
- `crossfade`: 过渡时间（秒，默认 0.15）
- `blend_mode`: `linear` / `cubic` / `sinusoidal` / `cubic_in_out`（默认 cubic_in_out）
- `--auto-rule`: 自动生成 True/False 转换规则，源状态名 → 目标状态名规则
- `--condition type:args`: 指定转换条件
  - `bool:VarName` — bool 变量
  - `not_bool:VarName` — 反向 bool
  - `float_compare:VarName:Op:Val` — Op: `Less`/`Greater`/`Equal`/`NotEqual`/`LessOrEqual`/`GreaterOrEqual`
  - `time_remaining:seconds` — 剩余时间 < 秒数
- `--bidirectional`: 同时创建 A→B 和 B→A 两条转换（条件自动反向）

**`add_node` 参数**:
- 节点类型: `sequence_player` / `blend_space_player`
- `--loop`: 是否循环播放
- `--pin-x VarName`: 将变量 X 连接到节点的 X 输入端
- `--pin-y VarName`: 将变量 Y 连接到节点的 Y 输入端（仅 blend_space_player）

### 2.9 PIE 控制

```bash
bridge.py pie status                    # 查询 PIE 状态
bridge.py pie start                     # 启动 PIE（默认：视口内）
bridge.py pie start standalone          # 独立进程
bridge.py pie start mobile              # 移动端预览
bridge.py pie start vulkan              # Vulkan 预览
bridge.py pie start vr                  # VR 预览
bridge.py pie start simulate            # 模拟模式（无玩家）
bridge.py pie start --clients 2 --listen    # 2 客户端 ListenServer
bridge.py pie start --clients 4 --dedicated # 4 客户端 DedicatedServer
bridge.py pie stop                      # 停止 PIE
bridge.py pie pause                     # 暂停 PIE
bridge.py pie resume                    # 恢复 PIE
bridge.py pie late_join                 # 添加一个客户端（多人运行时）
```

**start 参数**:
- 模式: `selected`(默认) / `standalone` / `mobile` / `vulkan` / `vr` / `simulate`
- `--clients N`: 客户端数量（默认 1）
- `--net_mode standalone|dedicated|listen`: 网络模式
- `--dedicated`: DedicatedServer 快捷
- `--listen` / `--server`: ListenServer 快捷
- `--viewport`: 视口内运行（默认 true）

### 2.10 输入模拟（PIE 运行时）

```bash
# 基础动作
bridge.py input_simulate attack            # R1 攻击按下
bridge.py input_simulate attack --hold 0.5  # R1 长按 0.5s（蓄力）
bridge.py input_simulate attack_release     # R1 松开
bridge.py input_simulate guard              # L1 防御
bridge.py input_simulate dodge              # ◻ 闪避
bridge.py input_simulate jump               # ✗ 跳跃
bridge.py input_simulate interact           # ○ 交互

# 复合动作
bridge.py input_simulate move --x 1.0 --y 0.0        # 向右移动
bridge.py input_simulate move --x 0.0 --y 1.0        # 向前移动
bridge.py input_simulate move --x 0.0 --y -1.0       # 向后移动
bridge.py input_simulate move --x 0.0 --y 1.0 --hold 2.0 # 持续前进 2 秒后自动停止
bridge.py input_simulate move_stop                    # 立即停止移动轴输入
bridge.py input_simulate look --x 0.5 --y 0.0        # 视角右转
bridge.py input_simulate look --x 0.0 --y -0.5       # 视角下转
bridge.py input_simulate look_stop                    # 立即停止视角轴输入

# 道具/忍义手
bridge.py input_simulate prosthetic       # 义手忍具
bridge.py input_simulate use_item         # 道具使用
bridge.py input_simulate healing_gourd    # 伤药葫芦
bridge.py input_simulate grapple          # 钩索

# 系统
bridge.py input_simulate lock_on          # 锁定
bridge.py input_simulate crouch           # 蹲下
bridge.py input_simulate cycle_item_next  # 切换道具下一个
bridge.py input_simulate pause            # 暂停
bridge.py input_simulate menu             # 菜单

# 带延迟（测试序列用）
bridge.py input_simulate attack --delay 0.5           # 0.5s 后攻击
bridge.py input_simulate attack --hold 0.3 --delay 1.0 # 1s 后长按攻击 0.3s
```

`move` 与 `look` 不带 `--hold` 时只注入一帧；带 `--hold` 时会逐帧重注入，并在到期后自动归零。执行对应的 `move_stop` 或 `look_stop` 可以提前取消保持并立即归零。

**支持的 action**:
`attack`, `attack_release`, `guard`, `guard_release`, `dodge`, `dodge_release`,
`jump`, `jump_release`, `interact`, `use_item`, `healing_gourd`, `grapple`,
`prosthetic`, `lock_on`, `crouch`, `move`, `move_stop`, `look`, `look_stop`, `cycle_item_next`,
`cycle_item_prev`, `pause`, `menu`

**可选参数**:
- `--x <value>`: X 轴值（move/look）
- `--y <value>`: Y 轴值（move/look）
- `--hold <seconds>`: 持续注入指定时间后自动释放（适用于按钮与 move/look 轴输入）
- `--delay <seconds>`: 延迟执行

### 2.11 编辑器生命周期

```bash
bridge.py editor start     # 启动编辑器（自动清理 cmd 窗口，等待最多 90s）
bridge.py editor stop      # 关闭编辑器 + LiveCoding/Trace 子进程
bridge.py editor restart   # 重启编辑器
```

**启动流程**：`DETACHED_PROCESS` 无 cmd 窗口 → 等待 TCP 9877 就绪（最多 90 秒）
**关闭流程**：杀 UnrealEditor + LiveCodingConsole + TraceServer + CEFSubProcess → 确认进程已退出
**重启流程**：stop → 等 2 秒 → start

### 2.12 崩溃追踪

```bash
bridge.py crash check      # 检测最近是否崩溃
bridge.py crash analyze    # 分析崩溃：错误信息、调用栈、源码定位、自动修复
bridge.py crash list       # 列出历史崩溃报告
```

**分析来源**：
- `Saved/Crashes/UECC-*/CrashContext.runtime-xml` — 错误类型、调用栈模块
- `Saved/Crashes/UECC-*/*.log` / `Saved/Logs/Sekiro.log` — Fatal/Assert 日志行
- 自动提取项目源码文件路径和行号（`sourceHints`）

### 2.13 工具列表

```bash
bridge.py tools    # 列出所有可用工具
```

---

## 3. 安全规则

| 操作 | 风险 |
|------|------|
| `asset delete` | 不可逆删除资产 |
| `asset create` | 创建新资产 |
| `compile all` | 可能触发大量编译 |
| `blueprint create` | 创建新 Blueprint |
| `blueprint addvar/addfunc/addnode` | 修改 Blueprint 结构 |
| `blueprint layout` | 移动图中节点位置 |
| `enhanced_input map_key/unmap_key` | 修改输入映射配置 |
| `enhanced_input configure_triggers` | 修改触发器配置 |
| `anim_blueprint add_state/add_transition/add_node/delete_transition/rename_node` | 修改动画蓝图图结构 |
| `anim_blueprint set_anim_class` | 修改角色绑定的动画蓝图 |
| `pie start` | 启动 PIE 会话 |
| `editor start/stop/restart` | 编辑器启停 |
| `console` 带写入命令 | 可能修改编辑器状态 |
| `input_simulate` | 注入玩家输入（PIE 中） |

**只读操作**（无需确认）：`query`, `asset list`, `asset info`, `ping`, `tools`, `console "stat*"`, `pie status`, `crash check/list`

**确认格式**：`"即将执行 [命令]。这可能 [影响说明]。是否继续？"`

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
| 标准流编码错误 | CLI 入口已把 stdout/stderr 固定为 UTF-8，并以 `backslashreplace` 兜底非法字符；若仍报错，检查调用是否绕过 `bridge.py` 入口 |

---

## 5. 工作流示例

### C++ 编译 + 自动修复

```
bridge.py compile cpp
  → 解析 UBT 输出 → 返回错误列表（file/line/code/message）
  → AI 自动读取错误文件 → 修复 → 重新 compile cpp
  → 最多 3 轮，超限请求用户介入
```

### 运行时崩溃追踪 + 修复

```
bridge.py crash check → bridge.py crash analyze
  → 读取 CrashContext + 日志 → 定位源码 → 自动修复
```

### 创建 InputAction + 绑定按键

```
bridge.py enhanced_input create_action /Game/Input/IA_Fire bool
bridge.py enhanced_input create_context /Game/Input/IMC_Combat
bridge.py enhanced_input map_key /Game/Input/IMC_Combat /Game/Input/IA_Fire LeftMouseButton
```

### 创建 AnimBlueprint + 添加状态机

```
bridge.py anim_blueprint create /Game/Anim/ABP_Player /Game/Anim/SK_Player
bridge.py anim_blueprint add_state /Game/Anim/ABP_Player Idle
bridge.py anim_blueprint add_state /Game/Anim/ABP_Player Run
bridge.py anim_blueprint add_transition /Game/Anim/ABP_Player Idle Run 0.15 cubic --auto-rule
bridge.py anim_blueprint add_node /Game/Anim/ABP_Player Idle sequence_player /Game/Anim/A_Idle
```

### PIE 输入序列测试

```
bridge.py pie start standalone
bridge.py input_simulate move --x 0.0 --y 1.0 --hold 2.0
bridge.py input_simulate attack --delay 0.2
bridge.py input_simulate attack --delay 0.5
bridge.py input_simulate attack --delay 0.5
bridge.py input_simulate guard --hold 1.0
bridge.py pie stop
```

### 编译后启动编辑器

```
bridge.py compile cpp  → 通过
bridge.py editor start  → 等待就绪
bridge.py ping → pong
```
