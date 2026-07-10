# Lua 代码规范

本文是项目 Lua 脚本的统一代码规范。以后新增或修改 `Content/Script/**/*.lua` 时必须遵守本文；历史 Lua 文件可以在业务改动时逐步补齐注释和结构，不要求只为格式化做大范围无意义改动。

本规范基于当前项目已有 Lua 代码整理，主要参考：

- `Content/Script/Animation/Base/*.lua`
- `Content/Script/Animation/Sekiro/*.lua`
- `Content/Script/Gameplay/**/*.lua`
- `Docs/lua-anim-blueprint-authoring-guide.md`

`Plugins/UnLua*`、`Plugins/UnLuaExtensions*` 下的 Lua 多为插件或第三方代码，不套用本项目风格，除非明确是在项目侧扩展。

## 一、核心原则

Lua 是脚本编排层，负责把 C++ 暴露的通用接口组合成玩法、输入、UI、动画状态机等具体工作流。

- C++ 提供稳定、通用、可复用的 `UFUNCTION` / 属性 / 组件接口。
- Lua 负责业务决策、状态切换、输入意图、UI 显隐、动画资源选择。
- Lua 不硬编码本机磁盘路径，不直接依赖编辑器临时状态。
- Lua 模块只暴露当前模块需要给 C++ / UnLua 调用的函数，其余逻辑保持 `local` 或类内方法。
- 每个新增业务模块必须有中文说明注释，复杂函数必须解释意图、输入来源和边界条件。

## 二、文件与编码

- 项目 Lua 放在 `Content/Script/` 下，模块名从该目录开始计算。
- 文件编码使用 UTF-8，无 BOM。
- 缩进统一 4 个空格，不使用 Tab。
- 行尾遵循项目 `.editorconfig`，保持 CRLF。
- 文件名使用模块或类名，例如 `SKInputManager.lua`、`GroundLocomotion.lua`、`Library.lua`。
- 临时 Lua 调试代码不要提交到业务模块；确实需要临时脚本时放到约定的临时目录，并在完成后清理。

模块路径示例：

```text
Content/Script/Gameplay/Sekiro/Input/SKInputManager.lua
=> require("Gameplay.Sekiro.Input.SKInputManager")
```

## 三、模块结构

每个 Lua 文件按以下顺序组织：

1. 模块说明注释。
2. `require` 依赖。
3. 本模块类表 / 数据表 / 常量表。
4. `local` helper 函数。
5. 类方法或模块方法。
6. C++ / UnLua 调用的 override 方法，例如 `Tick`、`OnMove`、`BeginPlay`。
7. `return`。

类模块推荐写法：

```lua
-- 管理玩家输入到角色意图的转换。
-- 本模块只编排 C++ 暴露的输入、移动和缓冲接口，不直接操作 Enhanced Input 资产。
local LuaComponent = require("Gameplay.Base.LuaComponent")

local SKInputManager = LuaComponent:Extend("SKInputManager", {
    Debug = false,
})

local function clamp(value, min_value, max_value)
    return math.max(min_value, math.min(value, max_value))
end

function SKInputManager:Construct(_context)
    self:LogDebug("Construct", "input lua host constructed")
end

function SKInputManager:Tick(_context, delta_seconds)
    local delta = delta_seconds or 0
    self:SetDodgeHoldTime(self:GetDodgeHoldTime() + delta)
    return true
end

return SKInputManager:Export()
```

纯数据模块推荐写法：

```lua
-- GroundLocomotion 动画层的数据定义。
-- 状态、调参和曲线名集中放在这里，状态机逻辑文件只消费这些语义名。
local AnimAssets = require("Animation.Sekiro.AnimAssets")

local M = {}

M.LayerName = "GroundLocomotion"

M.State = {
    Idle = "Idle",
    Cycle = "Cycle",
    Stop = "Stop",
}

M.Tuning = {
    MovingSpeedThreshold = 3, -- 低于该速度认为角色静止，避免动画在微小速度下抖动。
}

M.Assets = AnimAssets.Locomotion

return M
```

## 四、命名规范

- 类表 / 模块表使用 `PascalCase`：`SKInputManager`、`LuaComponent`、`GroundLocomotion`。
- 项目 Gameplay Lua 类沿用 `SK` 前缀：`SKHUD`、`SKUIManager`、`SKCameraManager`。
- 动画蓝图入口使用 `ABP_角色名`：`ABP_Sekiro`。
- 局部变量和局部 helper 函数使用 `snake_case`：`read_context_index`、`move_x`、`delta_seconds`。
- 类方法使用 `PascalCase`，并用冒号声明：`function SKCameraManager:RefreshLockOnTarget(...)`。
- C++ / UnLua 调用方法直接使用 C++ 事件名：`Tick`、`OnMove`、`OnDodgeCompleted`、`BeginPlay`。
- 布尔字段沿用 UE 风格时允许 `b` 前缀：`bIsMoving`、`bHasSmoothedScreenPosition`。
- 枚举和配置表字段使用 `PascalCase`：`State.Idle`、`Tuning.RunSpeed`、`CameraMode.LockOn`。
- 动画资源名按语义分段，使用下划线连接：`Walk_Forward_Start`、`Sprint_Forward_Loop`。
- 禁止新增单字母下划线前缀变量，例如 `S_Value`、`R_Data`。

`local M = {}` 只用于纯数据模块或小型库模块。业务类模块必须使用清晰类名。

## 五、注释规范

Lua 新代码要求详细注释，但注释要解释“为什么”和“边界”，不要重复代码字面意思。

必须写注释的位置：

- 文件顶部：说明模块职责、和 C++ / 蓝图 / 其他 Lua 模块的边界。
- 非平凡 public 方法：说明调用时机、输入来源、返回值含义。
- 状态机转换：说明进入条件的设计意图，尤其是曲线门控、最短时间、取消窗口。
- 配置表和阈值：说明单位、来源或调参影响。
- C++ 桥接：说明调用的 C++ 接口承担什么职责，Lua 是否需要兜底。
- `pcall`、`nil` fallback、兼容旧接口等防御代码：说明为什么需要防御。

推荐使用 EmmyLua 注释描述函数签名：

```lua
---处理移动输入，并把输入强度直接映射成 C++ MovementTier。
---这里是 C++ / UnLua 调用入口，业务规则直接写在入口内，避免 OnMove 再转发到 HandleMove / ResolveMovementTier。
---@param input_x number|nil 屏幕空间横向输入，来自 Enhanced Input。
---@param input_y number|nil 屏幕空间纵向输入，来自 Enhanced Input。
---@return boolean handled 返回 true 表示 Lua 已处理本次输入事件。
function SKInputManager:OnMove(_context, input_x, input_y)
    local x = input_x or 0
    local y = input_y or 0
    local amount = math.min(math.sqrt(x * x + y * y), 1)

    if self:IsOwnerCrouched() then
        self:SetMovementTierByName("Crouch")
    elseif self:IsWalkHeld() or amount <= self.AnalogWalkEnterThreshold then
        self:SetMovementTierByName("Walk")
    else
        self:SetMovementTierByName("Run")
    end

    return true
end
```

配置项注释示例：

```lua
M.Tuning = {
    StartToCycleMinTime = 0.48, -- Start 至少播放到该秒数后才允许进入 Cycle，避免起步动画被过早截断。
    CurveGateMaxWaitTime = 0.20, -- 曲线缺失或未触发时的最大等待时间，防止状态机卡死。
}
```

无效注释示例：

```lua
-- 获取速度
local speed = self:GetSpeed()
```

这种注释只重复代码，不提供设计信息。更好的写法是：

```lua
-- 输入释放后的短暂残留速度不应立即触发 Stop，先交给曲线门控决定退出时机。
local speed = self:GetSpeed()
```

## 六、格式细则

- 每个函数之间空一行。
- `require` 分组：基础库、项目基类、同域模块依次排列，中间可空一行。
- 表项多行书写并保留尾随逗号。
- 多行函数调用的参数每行一个语义块，右括号单独收尾或跟随最后一行，以现有上下文为准。
- 条件表达式跨行时，续行缩进 8 个空格，并把 `and` / `or` 放在行首。
- 简单卫语句允许早返回。复杂嵌套可以拆成真正复用的语义函数；禁止只为了转发入口而创建 `OnMove -> HandleMove -> Helper` 这类空壳链路。
- 字符串默认使用双引号。只有和已有外部示例或调试器代码保持一致时才使用单引号。

条件换行示例：

```lua
if self:IsDodgeHeld()
    and self:GetDodgeHoldTime() >= self:GetSprintHoldThreshold()
    and amount > 0.1
    and not self:IsOwnerFalling() then
    return "Sprint"
end
```

多行调用示例：

```lua
local created = self:CreateWidgetByPath(
    name,
    widget_class_path or "",
    layer_name or LayerName.HUD,
    z_order_offset or 0)
```

## 七、逻辑写法

- 优先使用 `local`，禁止无意创建全局变量。
- 读取 C++ / UObject 字段时，可能失败的地方使用 `pcall` 或基类封装函数。
- 判断 `nil` 时使用显式比较：`value == nil`、`value ~= nil`。
- 判断布尔语义时使用 `== true` / `~= true`，避免 `nil`、`false`、`0` 混淆。
- 对数值输入提供 fallback：`local delta = delta_seconds or 0`。
- 高频 `Tick` 中避免无意义创建大表；确实为了表达配置创建小表时要保持局部、短生命周期。
- Debug 输出走 `LogDebug` / `LogDebugFlag`，不要在正式逻辑里散落裸 `print`。
- 返回值要和 C++ 调用方约定一致：事件函数通常返回 `true` 表示 Lua 已处理，创建失败或无效输入才返回 `false`。

## 八、UnLua 与 C++ 桥接

Gameplay 组件类使用 `LuaComponent:Extend`。Lua 文件按“C++ 子类 override”的方式编写：C++ 调用 `Tick`、`OnMove`、`BeginPlay` 时，Lua 类中提供同名方法即可。

- 不写导出函数列表。`LuaComponent:Export()` 会自动导出类上的 public 方法。
- 不写旧前缀方法。输入、摄像机、UI 等事件逻辑应直接写在 `Tick`、`OnMove`、`OnDodgeCompleted`、`BeginPlay` 这类方法里。
- 禁止为了“适配签名”额外套一层 `OnMove -> HandleMove -> Helper` 的业务转发链。只有确实被多个入口复用、并且名字能表达独立语义的代码，才允许抽成 `local` 函数或类方法。
- `Construct` 只负责 Lua 实例初始化；不要把每帧事件再转发到同名空壳方法里。
- 函数名来自变量或可能冲突时，用 `self:CallCpp(function_name, ...)` 显式转发。
- 直接访问 C++ 暴露字段或方法前，确认字段来自当前上下文、运行时上下文或基类代理。

override 示例：

```lua
function SKCameraManager:Tick(_context, delta_seconds)
    self:RefreshCachedCameraComponents()

    if self:IsLockOnEnabled() then
        self:UpdateLockOnCamera(delta_seconds or 0)
    end

    return true
end
```

错误示例：

```lua
function SKCameraManager:Tick(_context, delta_seconds)
    return self:UpdateCamera(delta_seconds)
end
```

上面的写法只是在函数之间转发，业务入口被拆散到空壳方法里，后续调试时需要顺着调用链跳转，不符合项目 Lua 风格。

## 九、动画 Lua 约定

动画 Lua 继续遵守 `Docs/lua-anim-blueprint-authoring-guide.md`，并补充以下风格要求：

- `ABP_xxx.lua` 只负责创建状态机、注册动画层、声明 `AnimGraph()` 输出。
- `AnimAssets.lua` 只放资源路径，不写状态逻辑。
- `Layer/<LayerName>/Library.lua` 只放状态、调参、曲线名、动画设置等数据。
- 具体状态机文件负责状态判断、转移条件和动画提交。
- 状态名统一来自 `Library.State`，不要在状态机里散落裸字符串。
- 转移函数命名必须使用 `CanEnter_<From>_<To>`，基类会自动识别这个模式。
- 状态动画函数命名必须使用 `UpdateAnimation_<State>`。
- 意图判断函数使用 `Wants<State>`，动画选择函数使用 `Play...` / `Sample...`。
- 播放动画优先调用 `PlaySequence`、`SampleBlendSpace1D` 等基类接口，不直接调用底层 C++ Pose 提交函数。

动画状态机建议按以下顺序组织：

1. 运行时事实读取：`GetSpeed`、`IsMoving`、`IsGrounded`。
2. 方向、档位、曲线辅助函数。
3. 门控和时间判断。
4. `Wants<State>` 意图函数。
5. `UpdateAnimation_<State>` 与 `Play...` / `Sample...`。
6. `CanEnter_<From>_<To>` 转移函数。

复杂转移必须说明原因：

```lua
---Start 进入 Cycle 需要同时满足最短播放时间和曲线门控。
---这样可以保留起步重心转移，曲线缺失时再由最大等待时间兜底。
function GroundLocomotion:CanEnter_Start_Cycle()
    return self:IsStartReadyForCycle()
end
```

## 十、UI / 输入 / 摄像机 Lua 约定

- UI 层名、ZOrder、颜色、距离缩放等配置集中在文件顶部的表里。
- 输入事件只转换为“意图”和“缓冲”，不要在输入模块里直接写复杂战斗结算。
- 摄像机模块只处理视角模式、旋转设置和目标朝向，不直接写角色移动规则。
- UI、输入、摄像机调用 C++ 的函数应保持语义化，例如 `SetMovementTierByName`、`ApplyControllerYawForScript`、`SetLockOnIndicatorVisible`。
- 每个跨系统调用点要通过函数名或注释说明影响范围，避免 Lua 文件之间隐式耦合。

## 十一、提交前检查

修改 Lua 后至少检查：

- 文件在 `Content/Script/` 下，模块名和 `require` 路径一致。
- 没有新增全局变量。
- 新增业务函数有中文注释，复杂阈值有单位或调参说明。
- C++ 调用的事件方法使用原名 override，文件中没有导出函数列表，也没有旧前缀方法。
- 动画状态名、资源名、曲线名来自集中数据表。
- Debug `print` 已移除或改为 `LogDebug`。
- 触碰动画 Lua 时，同时确认 `Docs/lua-anim-blueprint-authoring-guide.md` 的约定没有被破坏。
