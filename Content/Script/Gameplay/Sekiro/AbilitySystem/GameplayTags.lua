-- Lua 类型：纯 Lua 配置模块。项目 GameplayTag 字典的唯一手写定义，供 LuaGameplayEditor 通用模块生成原生 DataTable。
-- 每级键生成一个点分标签；字符串是该标签的说明，_Comment 只描述当前分支，不生成额外标签。
-- 本文件只定义标签，不向任何角色添加状态；规范文件名与生成资产来源元数据保持一致，修改后通过主菜单“Lua玩法 → GameplayTag 的 Lua 导入”重新生成。

return {
    Locomotion = {
        _Comment = "Motion Matching 基础移动搜索域；标签只筛选合法数据库，不直接切换动画资产",
        Mode = {
            _Comment = "角色朝向与移动方向的关系",
            Free = "自由移动：Facing 通常跟随移动方向",
            Locked = "锁定移动：Facing 面向目标，Trajectory 保留输入移动方向",
        },
        Gait = {
            _Comment = "基础移动的目标速度档位",
            Walk = "步行或低速基础移动候选",
            Run = "常规跑步基础移动候选",
            Sprint = "冲刺基础移动候选",
        },
        Stance = {
            _Comment = "基础移动姿态域",
            Standing = "站立姿态候选",
            Crouching = "蹲伏姿态候选",
            WeaponDrawn = "持刀姿态候选；仅在对应数据库具备足够素材时启用",
        },
    },
    State = {
        _Comment = "角色状态及外部效果授予的门禁",
        Life = {
            _Comment = "Survival 维护的生命流程镜像，不由外部标签写入触发状态转换",
            Dying = "生命耗尽，死亡流程正在收尾，普通行动与资源请求关闭",
            Dead = "死亡流程已收尾，只有显式回生流程可以恢复生命轮次",
            Reviving = "回生准备阶段，资源提交成功前保持行动门禁",
        },
        Posture = {
            _Comment = "躯干崩溃状态和躯干伤害免疫",
            Broken = "Survival 维护的躯干崩溃镜像，清零资源不等于解除崩溃",
            Immune = "GE 或外部规则授予：阻止普通躯干伤害，不改变已裁决的攻防结果",
        },
        Damage = {
            _Comment = "生命伤害门禁",
            Immune = "GE 或外部规则授予：阻止普通生命伤害，不阻止合法治疗",
        },
        Revive = {
            _Comment = "回生资格门禁，不代表回生次数或费用",
            Blocked = "GE 或外部规则授予：禁止开始回生流程",
        },
    },
}
