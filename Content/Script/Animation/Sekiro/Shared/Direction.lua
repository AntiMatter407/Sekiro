-- Lua 类型：纯 Lua 类/数据/工具；self（如有）仅表示 Lua 表，不是 UObject。
-- Sekiro 动画蓝图的离散方向定义与角度分类。
-- 本模块只处理纯数值，不读取 AnimInstance，也不选择动画资产；运行时入口负责把结果写入生成类变量。

---@alias SekiroCardinalDirection number
---@alias SekiroOctantDirection number

---@class SekiroDirectionLibrary
---@field Cardinal table<string, SekiroCardinalDirection> 四方向枚举值，顺序与生成 Graph 的 BlendList 输入一致。
---@field Octant table<string, SekiroOctantDirection> 八方向枚举值，顺序按顺时针排列。
---@field CardinalAngle table<SekiroCardinalDirection, number> 四方向枚举对应的角色局部中心角。
---@field OctantAngle table<SekiroOctantDirection, number> 八方向枚举对应的角色局部中心角。
local Direction = {}

Direction.Cardinal = {
    Forward = 0,
    Back = 4,
    Left = 2,
    Right = 6,
}

Direction.Octant = {
    Forward = 0,
    ForwardLeft = 1,
    Left = 2,
    BackLeft = 3,
    Back = 4,
    BackRight = 5,
    Right = 6,
    ForwardRight = 7,
}

Direction.CardinalAngle = {
    [Direction.Cardinal.Forward] = 0,
    [Direction.Cardinal.Back] = 180,
    [Direction.Cardinal.Left] = -90,
    [Direction.Cardinal.Right] = 90,
}

Direction.OctantAngle = {
    [Direction.Octant.Forward] = 0,
    [Direction.Octant.ForwardLeft] = -45,
    [Direction.Octant.Left] = -90,
    [Direction.Octant.BackLeft] = -135,
    [Direction.Octant.Back] = 180,
    [Direction.Octant.BackRight] = 135,
    [Direction.Octant.Right] = 90,
    [Direction.Octant.ForwardRight] = 45,
}

---把任意角度规范到 -180..180；180 保留为正值，便于稳定识别后向。
---@param angle number|nil 需要规范化的角度，单位为度；nil 按 0 处理。
---@return number normalized_angle 规范化后的角度，范围为 -180..180。
function Direction.NormalizeAngle(angle)
    local normalized_angle = (angle or 0) % 360
    if normalized_angle > 180 then
        normalized_angle = normalized_angle - 360
    end
    return normalized_angle
end

---把角色局部移动角分类为四方向，不使用字符串拼接推导动画名。
---该结果用于锁定移动、Step 及一次性动作的原生 BlendList 选择器。
---@param angle number|nil 角色局部移动角，0 为前、90 为右、-90 为左、180 为后。
---@return SekiroCardinalDirection direction 最接近输入角的四方向枚举值。
function Direction.ClassifyCardinal(angle)
    local normalized_angle = Direction.NormalizeAngle(angle)
    local absolute_angle = math.abs(normalized_angle)
    if absolute_angle <= 45 then
        return Direction.Cardinal.Forward
    end
    if absolute_angle >= 135 then
        return Direction.Cardinal.Back
    end
    return normalized_angle < 0 and Direction.Cardinal.Left or Direction.Cardinal.Right
end

---把前后、左右输入轴转换为角色局部方向角。
---该入口用于 Dodge 以及锁定基础素材选区；实际轨迹对齐仍使用 Movement 发布的 MoveDirectionAngle。
---@param forward_amount number|nil 前后输入分量，正数为前、负数为后。
---@param lateral_amount number|nil 左右输入分量，正数为右、负数为左。
---@return number direction_angle 输入向量对应的局部方向角；零向量回退为 0 度。
function Direction.GetAngleFromAxes(forward_amount, lateral_amount)
    local forward = forward_amount or 0
    local lateral = lateral_amount or 0
    if math.abs(forward) <= 0.001 and math.abs(lateral) <= 0.001 then
        return 0
    end

    return Direction.NormalizeAngle(math.deg(math.atan(lateral, forward)))
end

---把自由移动起步角分类为前向或最近的左右转身，永远不返回 Back。
---自由模式会由 Movement Lua 把角色转向输入世界方向；Back 素材表示身体朝前后退，只适用于锁定移动。
---@param angle number|nil Movement Lua 在旋转角色前锁存的局部移动角，单位为度。
---@return SekiroCardinalDirection direction 前向或左右转身枚举；精确 180 度稳定选择 Right。
function Direction.ResolveFreeTurnDirection(angle)
    local normalized_angle = Direction.NormalizeAngle(angle)
    if math.abs(normalized_angle) <= 45 then
        return Direction.Cardinal.Forward
    end

    return normalized_angle < 0 and Direction.Cardinal.Left or Direction.Cardinal.Right
end

---把角色局部前后/左右输入分量分类为四方向。
---该入口用于 DodgeDirection 与 DodgeDirectionLateral；二者是输入轴而不是角度，不能直接传给 ClassifyCardinal。
---@param forward_amount number|nil 前后输入分量，正数为前、负数为后。
---@param lateral_amount number|nil 左右输入分量，正数为右、负数为左。
---@return SekiroCardinalDirection direction 最接近输入向量的四方向枚举值；零向量回退为前向。
function Direction.ClassifyCardinalFromAxes(forward_amount, lateral_amount)
    return Direction.ClassifyCardinal(
        Direction.GetAngleFromAxes(forward_amount, lateral_amount))
end

---按可配置的前后扇区边界分类锁定移动方向。
---该入口只服务锁定 Locomotion；Dodge 等需要“几何最近方向”的调用继续使用 ClassifyCardinal。
---@param angle number|nil 角色局部移动角，0 为前、90 为右、-90 为左、180 为后。
---@param forward_boundary_angle number|nil Forward 扇区的绝对角上限；nil 使用传统 45 度。
---@param back_boundary_angle number|nil Back 扇区的绝对角下限；nil 使用传统 135 度。
---@return SekiroCardinalDirection direction 按前、侧、后非等分扇区选择的四方向枚举值。
function Direction.ClassifyLockedCardinal(angle, forward_boundary_angle, back_boundary_angle)
    local normalized_angle = Direction.NormalizeAngle(angle)
    local absolute_angle = math.abs(normalized_angle)
    local forward_boundary = math.max(0, math.min(forward_boundary_angle or 45, 180))
    local back_boundary = math.max(
        forward_boundary,
        math.min(back_boundary_angle or 135, 180))

    if absolute_angle <= forward_boundary then
        return Direction.Cardinal.Forward
    end
    if absolute_angle >= back_boundary then
        return Direction.Cardinal.Back
    end
    return normalized_angle < 0 and Direction.Cardinal.Left or Direction.Cardinal.Right
end

---优先保持当前四向素材，避免斜向输入同时触发方向换腿和 ActorYaw 转向。
---Forward/Back 按配置扇区加滞回保持；Left/Right 以相邻主轴中点 45 度加滞回保持。
---当前素材超出允许残差后才重新分类，因此纯后退仍会切 Back，而后左/后右可以沿用进入斜向前的素材。
---@param angle number|nil 角色局部移动角，单位为度。
---@param current_direction SekiroCardinalDirection|nil 上一帧已选择的四方向；nil 时直接分类。
---@param hysteresis_angle number|nil 当前素材离开基础范围时额外保留的容差；nil 使用 10 度。
---@param forward_boundary_angle number|nil Forward 扇区的绝对角上限；nil 使用传统 45 度。
---@param back_boundary_angle number|nil Back 扇区的绝对角下限；nil 使用传统 135 度。
---@return SekiroCardinalDirection direction 本帧稳定后的四方向枚举值。
function Direction.ResolveCardinalWithHysteresis(
    angle,
    current_direction,
    hysteresis_angle,
    forward_boundary_angle,
    back_boundary_angle)
    local normalized_angle = Direction.NormalizeAngle(angle)
    local absolute_angle = math.abs(normalized_angle)
    local hysteresis = math.max(hysteresis_angle or 10, 0)
    local forward_boundary = math.max(0, math.min(forward_boundary_angle or 45, 180))
    local back_boundary = math.max(
        forward_boundary,
        math.min(back_boundary_angle or 135, 180))

    local current_axis = Direction.CardinalAngle[current_direction]
    if current_axis ~= nil then
        local current_residual = math.abs(
            Direction.NormalizeAngle(normalized_angle - current_axis))
        local retained_residual = 45 + hysteresis
        if current_direction == Direction.Cardinal.Forward then
            retained_residual = forward_boundary + hysteresis
        elseif current_direction == Direction.Cardinal.Back then
            retained_residual = 180 - back_boundary + hysteresis
        end
        if current_residual <= retained_residual then
            return current_direction
        end
    end

    return Direction.ClassifyLockedCardinal(
        normalized_angle,
        forward_boundary,
        back_boundary)
end

---把角色局部移动角分类为八方向，供 Jump 的 Start、InAir 和 Land 原生选择器使用。
---@param angle number|nil 角色局部移动角，单位为度。
---@return SekiroOctantDirection direction 最接近输入角的八方向枚举值。
function Direction.ClassifyOctant(angle)
    local normalized_angle = Direction.NormalizeAngle(angle)
    local counter_clockwise_angle = -normalized_angle
    if counter_clockwise_angle < 0 then
        counter_clockwise_angle = counter_clockwise_angle + 360
    end
    return math.floor((counter_clockwise_angle + 22.5) / 45) % 8
end

---计算精确输入角与四方向素材中心角之间的残差，供后续 Orientation Warping 使用。
---@param angle number|nil 未量化的角色局部移动角，单位为度。
---@param direction SekiroCardinalDirection 四方向素材选择结果。
---@return number residual_angle 精确方向相对素材中心方向的有符号角度差。
function Direction.GetCardinalResidual(angle, direction)
    local source_angle = Direction.CardinalAngle[direction] or 0
    return Direction.NormalizeAngle((angle or 0) - source_angle)
end

---计算精确移动角与八方向 Jump 素材中心角之间的残差。
---八方向最近选择会把残差限制在约 22.5 度内，供下半身对齐真实空中轨迹。
---@param angle number|nil 未量化的角色局部移动角，单位为度。
---@param direction SekiroOctantDirection 八方向 Jump 素材选择结果。
---@return number residual_angle 精确方向相对素材中心方向的有符号角度差。
function Direction.GetOctantResidual(angle, direction)
    local source_angle = Direction.OctantAngle[direction] or 0
    return Direction.NormalizeAngle((angle or 0) - source_angle)
end

return Direction
