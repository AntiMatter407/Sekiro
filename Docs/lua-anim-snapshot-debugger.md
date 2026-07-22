# Lua 动画蓝图层级调试与快照回放

Lua 动画调试工具由实时层级视图、JSONL 快照记录器和编辑器时间轴查看器组成。三者读取同一份 UE 运行时动画节点数据，避免把源码声明误当作实际执行状态。

## 实时层级视图

在 PIE 或游戏控制台执行：

```text
Sekiro.LuaAnim.Debug
```

无参数时显示当前本地玩家 AnimInstance 的有效执行层级。显示规则如下：

- 从最终 Pose 根节点开始，递归显示所有权重有效的活跃节点；混合期间的多个有效分支全部保留，不省略中间节点。
- SequencePlayer、Slot、动态 Montage、Cached Pose、Blend、Layered Blend、Additive 和状态分支都各占一行，并保持 UE 实际求值层级。
- SequencePlayer 与 Montage 同时显示原生动画名和可解析出的 Lua 语义名；Montage 还显示槽位、当前位置和实际权重。
- 状态机显示当前状态；发生状态混合时同时显示上一状态、各状态权重和当前状态混合系数。
- 嵌套状态机保持原有层级。
- 未识别节点保留 UE 原始调试文本，不推测不存在的语义。

关闭视图：

```text
Sekiro.LuaAnim.Debug Off
```

该命令使用唯一屏幕消息通道。重复执行会替换当前视图，不会叠加输出。

## 快照记录

按默认 `0.15` 秒间隔开始一次新记录：

```text
Sekiro.LuaAnim.Snapshot
```

指定采样间隔，单位为秒：

```text
Sekiro.LuaAnim.Snapshot 0.25
```

停止并刷新当前文件：

```text
Sekiro.LuaAnim.Snapshot.Stop
```

每次开始命令都会结束旧 Session，并在 `Saved/LuaAnimSnapshots/` 创建新的 `.jsonl` 文件。记录器会立即保存 Start 帧，此后按间隔采样；任一活跃状态机、活跃分支、Sequence 或 Montage 发生离散变化时立即保存 `StateChanged` 帧，并从该时刻重新计算下一个定时采样点。同一游戏帧最多写入一个快照。

每个快照包含：

- ISO-8601 UTC 时间戳、Session 相对时间、帧序号和采样原因。
- AnimInstance 路径与实际 Lua 动画蓝图模块名。
- 全部活跃节点及其层级、权重、可靠解析出的输入和 UE 原始调试文本。
- 当前 AnimInstance 上全部 Blueprint 可见变量，包括 Lua AnimBlueprint 生成变量和原生父类公开给动画蓝图的输入变量。
- 当前帧实际求值出的 Attribute、MorphTarget 和 Material 曲线名称与数值；Transition 使用的曲线值也继续保留在对应表达式采样中。
- SequencePlayer 与动态 Montage 的原生动画名，以及动画蓝图模块 `ResolveDebugAnimationName` 返回的 Lua 语义名。
- 单一动画输出或 `Pose#N` 临时别名；混合、分骨骼混合、Additive 等输出递归记录各输入别名和相对权重。
- 自上一快照以来实际执行过的 Transition、最终结果、表达式结果、属性实际值、期望值或阈值，以及求值时间。

实时视图和快照都保留全部有效混合贡献。无法可靠解释的输入仍保留在 `RawDebugLine`，不会生成猜测值。新增变量和曲线字段使用 JSONL Schema 2；查看器继续兼容缺少这些字段的 Schema 1 文件。

## 编辑器时间轴

通过编辑器 `Window > Lua Anim Snapshot Viewer` 打开查看器。查看器默认加载 `Saved/LuaAnimSnapshots/` 中最新的快照文件，也可以手动选择其他 `.jsonl` 文件。

- 时间轴上的每个点对应一个快照，并按 Session 时间排序。
- 鼠标滚轮以指针所在时间为中心缩放。
- 中键或右键拖拽平移时间轴。
- 点击时间点选择快照；悬停显示帧序号、UTC 时间、相对时间和采样原因。
- `Start`、`Interval` 与 `StateChanged` 使用不同颜色，其中状态变化点最醒目。
- `Fit All` 显示完整 Session，`Reset View` 恢复默认视野。
- 选择快照后，节点树默认完整展开；节点输入、状态权重、输出推导、全部动画蓝图变量、当前曲线值和 Transition 详情同步切换到该帧。

JSONL 采用一行一帧。单行损坏不会影响其他快照，查看器会跳过坏行并显示解析警告。

## Lua 动画名解析约定

动画蓝图 Lua 模块可以导出以下纯查询函数：

```lua
ResolveDebugAnimationName(native_asset_name) -> string|nil
```

调试器只在采样时调用。返回非空字符串时使用 Lua 语义名；否则保留 UE 原生动画短名。解析器不得修改动画状态或资产。

## 生成与验证要求

原生强类型 Transition 的参数采样节点由 Lua AnimBlueprint Factory 生成。修改调试采样实现后，需要重新编译插件并对目标动画蓝图执行 `Check Lua` 与 `Generate From Lua`，新的 Transition 快照字段才会生效。
