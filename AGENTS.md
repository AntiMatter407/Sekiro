# Sekiro → UE5.8

本项目是《只狼》面向 Unreal Engine 5.8.2 的升级版本，也是后续开发的主力工程。本目录是独立项目，不是 `F:/ProjectAI/Sekiro` 的子目录；后者仅作为 UE 5.2 历史参考保留。

## 技术栈

- **引擎**: Unreal Engine 5.8.2（`Sekiro.uproject` 的 `EngineAssociation` 当前解析到 `F:/UnrealEngine-5.8`）
- **语言**: C++（游戏核心 + 插件接口），Python（管线脚本），Lua（脚本层，UnLua）
- **构建**: Unreal Build Tool (UBT)

## 项目结构

@Docs/directory-structure.md

```
Source/Sekiro/          # 游戏代码（C++）— 使用 SK 前缀
Plugins/                # 插件（C++）— 通用插件使用能力语义前缀；项目专用插件才可使用 Sekiro 前缀
Script/                 # Python 管线脚本
Content/                # UE 资源
Tools/                  # 外部工具（Yabber, FlverToFbx, texconv）
.codex/skills/          # 项目技能定义
.codex/rules/           # 代码规则
```

## 技能（/ 命令）

| 技能 | 用途 |
|------|------|
| `/aibridge` | 通过 TCP JSON-RPC 操控 UE5 编辑器（查询/编译/蓝图/资产/输入） |
| `/sekiro-asset-import` | 只狼资产导入管线：解包→FLVER→JSON→UE 完整流程 |
| `/plan` | 方案设计 + 详细设计。分析需求，输出 `Docs/plan/<slug>.md`（进度+任务树）和 `Docs/design/<slug>.md`（单文档方案，子任务作为章节），追踪进度 |
| `/delegate` | 将复杂问题委托给新 subagent，净化上下文专注处理 |
| `/review` | 代码审查，风格问题自动修复，违规派发对应 Agent |

## Agent 边界（强制）

| Agent | 职责 | 代码位置 | 禁止 |
|-------|------|---------|------|
| gameplay-programmer | 游戏机制（战斗/移动/角色） | `Source/Sekiro/` | 修改 Plugins/、构建文件 |
| plugin-programmer | C++ 插件接口（零硬编码，只引用引擎） | `Plugins/` | 修改 Source/Sekiro/、硬编码路径 |
| review-agent | 代码审查（被 /review 调用） | — | — |
| expert-agent | 多角度分析（被 /reasoning 调用） | — | — |

## 编码规范

@.codex/rules/code-style.md
@Docs/lua-code-style.md
@.codex/rules/cpp-workflow.md
@.codex/rules/aibridge-workflow.md
@.codex/rules/bug-fix-workflow.md

**核心架构原则**：C++ 只提供通用接口（UFUNCTION），脚本层负责编排具体工作流。
**插件数据边界**：`Plugins/` 不得定义或校验具体项目的业务 IR 字段、状态枚举、资产路径和数据组合；插件只提供通用纯值传输、反射与资产操作能力。项目 Lua 可以在通用纯值树中自由扩展字段和嵌套结构而无需修改插件，领域结构、版本和语义校验由项目侧源码负责。
**C++ / Lua 枚举边界**：当 C++ 或引擎已经提供 `UENUM` 时，Lua 的传参、返回值、比较、缓存和 Lua 表键必须优先直接使用 `UE.EEnumType.Value`，C++ 的反射接口也必须直接接收或返回对应枚举。禁止把枚举先在 Lua 中转成名称字符串，再由 C++ 通过 `ByName`、`FromString` 或手写分支解析回枚举；不得以 `tostring(EnumValue)` 参与业务判断。只有日志展示、持久化、外部协议和明确要求可扩展字符串标识符的插件边界允许枚举与字符串转换，转换必须集中在边界适配层，不能形成运行时往返链。
**新增或修改 `Content/Script/**/*.lua` 时，必须先读取并遵守 `Docs/lua-code-style.md`，并补齐必要中文注释。**
**Lua 生成的 `.uasset` 默认视为构建产物；遇到动画蓝图、行为树或其他生成资产问题时，优先修改 Lua/C++ 源码并通过生成流程验证，尽量不直接编辑或保存二进制资产。只有用户明确要求生成、重建或修复该二进制资产时，才允许写入 `.uasset`。**
**Lua 对动画蓝图的动画图结构拥有全量所有权：每次 Lua → AnimBlueprint 都必须完整替换 AnimGraph、动画层图、状态机、状态图和过渡图，不得增量合并或保留这些图中的蓝图侧节点；变量、函数、EventGraph 等非动画图结构暂不执行全量替换。**
**Lua 对行为树的树结构拥有全量所有权：每次 Lua → BehaviorTree 都必须完整替换整棵树及其节点、装饰器、Service 和连接，不得增量合并或保留树编辑器中的手工结构；Blackboard、资产配置和其他非树结构暂不执行全量替换。**
**需要修改 `.h` / `.cpp` 时，必须先读取 cpp-workflow.md 然后按规则执行。**
**新增 C++ 函数时，完整的职责、参数、返回值和线程约束注释以 `.cpp` 实现处为准；`.h` 只保留必要的接口摘要、UHT 提示，以及无独立实现的模板/内联/纯虚函数文档。**
**操作 UE 编辑器时，必须先读取 aibridge-workflow.md 然后按规则执行。**
**分析未知的 Bug/问题/错误时，必须先读取 bug-fix-workflow.md 然后按规则执行。**

关键约定：
- `Source/Sekiro/` 使用 `SK` 缩写前缀（ASKCharacter、USKAnimInstance）
- 通用 `Plugins/` 的插件、模块、C++ 类型和 API 宏不得使用 `Sekiro` 前缀，应使用能力语义命名（例如 `LuaEditorExtensions`、`ULuaBehaviorTreeTask`）；仅项目专用插件可使用 `Sekiro` 前缀
- UPROPERTY 宏独占一行，变量下一行，同行中文注释
- 所有 Python 脚本使用 UTF-8 编码
- 所有项目 Lua 脚本使用 UTF-8 编码，4 空格缩进，详细中文注释
- 统一使用 4 空格缩进
- 禁止单字母下划线前缀
- 临时脚本写入 `Script/temp/`

## 工作流

```
/plan <需求>        → 方案设计 + 任务拆分 → Docs/plan/<需求>.md
/delegate <复杂问题>  → 开启 subagent 净化上下文 → 处理 → 返回结果
/review <范围>           → 审查报告 + 修复
```

## UE5.8 Motion Matching + GAS 约束

- 对应任务：`Docs/plan/motion-matching-gas-action-system.md`。
- 对应方案：`Docs/design/motion-matching-gas-action-system.md`。
- 基础 Locomotion 水平位移只能由最终动画 RootMotion 生成；MoveIntent 和未来轨迹只用于查询。
- 禁止用 `AddMovementInput` 或 CMC 输入加速度生成第二份基础移动水平位移。
- CharacterMovement 负责消费 RootMotion、碰撞、地面、重力、MovementMode 和网络移动基础，不是地面 Locomotion 位移生成者。
- Chooser 负责 Phase/Gait/Mode/Stance 候选门禁，Motion Matching 只在合法数据库中排序。
- 状态切换使用持久 Search Request，并由 PostSelection 确认；禁止依赖单帧 ForceInterrupt。
- 普通移动 ActorYaw 只允许在最终 RootMotion 协调链提交一次；Lua、CMC 自动旋转和旧方向重定向不得争抢所有权。
- Locomotion Animation 与 FullBody GA Montage 的 RootMotion 所有权必须互斥。
- Offset Root Bone Translation 默认关闭；Warping 和 IK 不得生成第二份胶囊位移。
- 从 UE5.2 复制来的完成记录只代表迁入基线；没有经过 UE5.8 编译、资产重建或运行时门禁的项目不得标记为 UE5.8 已验证。
- 独立旧项目 `F:/ProjectAI/Sekiro` 默认只读；只有用户明确要求跨项目同步时才允许修改。

## 默认执行范围与成本控制（强制）

- 默认只做当前步骤所需的简单文档、代码或脚本编辑，以及定位修改点必需的少量定向读取。
- 编译、测试、资产生成、编辑器查询与启停、PIE、输入模拟、全量扫描、大范围源码分析、批量处理和多 Agent 工作等高消耗操作，默认不执行；先列成待办交给用户执行。
- 当当前步骤使用已有或可编写的确定性脚本完成复杂操作，且该操作已经获得用户明确授权时，由 Agent 负责编写、执行、检查结果并处理必要重试，不把脚本或命令转交用户手工执行。脚本授权只覆盖脚本声明的操作范围，不自动授权 C++/Blueprint 编译、PIE、输入模拟或其他验证。
- 脚本执行过程中如果确认根因位于项目或通用插件，必须同步修复对应插件源码；不得只在项目脚本中绕过插件缺陷。若修复需要新的 C++ 二进制才能继续，Agent 应在完成源码修改后停止流程，请用户编译或明确授权 Agent 编译；不得用旧二进制兜底跳过该门禁。插件源码修复不自动授权插件编译，编译仍遵守验证规范。
- 待办应简要写明操作对象、执行步骤或命令、预期结果；用户返回结果后，再据此做下一步小范围修改。
- “按方案一步步来”“验证完再全量验证”“修复”等整体目标不视为执行全部流程的授权。用户单独回复“继续”时，视为明确授权 Agent 执行当前任务中已经说明的下一最小步骤及其必要的同范围操作；该授权可以覆盖该步骤已声明的编译、资产生成、编辑器操作、PIE 或输入模拟，但不得跨越后续步骤、扩大动画集合、启动全量验证或执行未预告的破坏性操作。
- 每次只推进当前最小步骤；不自动完成全量动作或扩大动画集合。Motion Matching 先以 Idle、Forward Start、Forward Run Loop、Forward Stop 最小动画组验收，全部通过后才安排全量 Locomotion 验证。
- “最小动画组”仅限制验证时使用的动画数据范围，不限制系统架构或源码形式。动画蓝图、其他蓝图资产、Lua 和 C++ 均沿用正式方案的同一套实现，按步骤逐步完善；不得为了最小动画组另建精简版、测试版或平行实现。验证完成后在原有实现中扩展动画数据，不另换一套系统。
- 此规则优先于项目其他工作流或技能中的自动编译、自动测试、自动启动编辑器和自动派发 Agent 要求；高消耗步骤应改为用户待办。

## 验证规范（强制）

- 用户没有明确要求验证时，默认只完成代码、脚本或文档编写，不执行任何编译、测试、资产生成或运行时验证。
- 默认禁止执行 UBT、UHT、Live Coding、C++ 编译、插件编译、Blueprint/AnimBlueprint 编译、Lua → 资产生成、Cook、自动化测试、PIE、输入模拟或运行游戏场景。
- 只有用户明确要求“编译”“生成资产”“运行测试”“PIE 测试”、指定具体验证方式，或以“继续”授权上一条回复已经明确说明的下一最小步骤时，才执行对应范围；不得把其中一项授权扩展到后续步骤或其他验证方式。
- 用户只要求修复或实现代码时，完成源码修改和必要的只读检查后即停止，并明确说明尚未编译或运行验证。

## 编码要求

所有 `.h` / `.cpp` 使用 **UTF-8 with BOM**。禁止用 Bash/PowerShell 的 `Set-Content` 等命令写入源码文件——只能用 Write/Edit 工具，否则中文注释会损坏。

所有对话、建议、文档使用简体中文。
