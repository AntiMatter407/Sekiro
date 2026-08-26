# GAS 数值基础系统

| 状态 | 创建 | 更新 |
|------|------|------|
| ✅ 已完成（编译验证） | 2026-08-26 | 2026-08-26 |

父需求：[Boss AI 与玩家对战基础闭环](boss-player-combat-foundation.md)，对应任务 2.1。

技术方案：[GAS 数值基础系统](../design/gas-numeric-foundation.md)。
关联独立子需求：[Gameplay C++ 插件与标签资产生成](lua-gameplay-tags.md)（父任务 2.6，编辑器窗口、标签 DataTable 与可扩展工具入口，不改变数值属性归属）。
已实现扩展：[生存组件 USKSurvivalComponent](survival-component.md)（全量躯干参数、生命周期策略与 Lua 迁移；验证记录见该子任务）。
该扩展还将剩余躯干数值参数全部迁入 AttributeSet，并将全部躯干逻辑迁至 Survival/对应 Lua；不计作本数值基础任务已经实现的内容。

## 需求与范围

玩家、AI 角色共享 GAS。生命、生命上限、攻击、护甲、架势、架势上限、架势恢复速度通过 AttributeSet 存储，
GameplayEffect 提交数值变化，ASC 提供 Blueprint/Lua 通用接口。角色初始数值从各自的平铺 Character Lua 表输入，全部生命/战斗数值统一存储在 USKCharacterAttributeSet，
禁止在角色 C++ 或后续 SurvivalComponent 中存储另一份权威属性。

本任务实现初始化、属性限制、伤害/治疗/架势数值提交、可配置护甲曲线计算、效果应用与移除、通知及现有架势迁移。
`USKSurvivalComponent`、死亡/回生生命周期、Boss 阶段、Weapon/Projectile 统一命中协议、完整 GameplayAbility 动作迁移不在范围内。

## 任务树

- ✅ 1. 建立独立任务与设计契约
  - ✅ 1.1 明确 AttributeSet / ASC / Lua / 后续 LifeComponent 边界
  - ✅ 1.2 固化初始化、资源上限、临时属性、效果句柄和数值通知规则
- ✅ 2. GAS 原生数值基础 (依赖: 1)
  - ✅ 2.1 添加角色 ASC、AttributeSet、Profile 和初始化数据契约
  - ✅ 2.2 实现数值 GameplayEffect、范围校验与实际结算结果
  - ✅ 2.3 实现属性快照、通知、护甲曲线与持有效果接口
- ✅ 3. 现有角色与 Lua 接入 (依赖: 2)
  - ✅ 3.1 玩家/AI 继承 GAS；按角色 Lua 表初始化，角色配置不再使用 Profile 优先路径
  - ✅ 3.2 移除 CombatComponent 架势数值副本和任意写入接口
  - ✅ 3.3 Lua 架势伤害/恢复改用 GE，恢复策略保留原渐进倍率
- ✅ 4. 验证与收尾 (依赖: 3)
  - ✅ 4.1 补齐 GAS 契约自动化测试代码（默认不执行）
  - ✅ 4.2 UBT 编译 SekiroEditor
  - ✅ 4.3 Lua 语法/文档静态检查、BOM 与差异检查
  - ✅ 4.4 同步父任务、实际接口与验证记录

## 完成标准

- 角色只持有一份对应 AttributeSet，重复接管不重复初始化。
- Health/Posture 的上限收敛同时影响实际资源值，移除上限增益不会造成隐藏回血。
- 非有限输入拒绝；伤害、治疗、架势变化输出实际变化而非原始请求量。
- 架势现有 Lua 不再调用 SetMaxPosture/SetCurrentPosture，不直接维护 GAS 属性副本。
- 通过 UBT 编译；明确区分已编译的测试代码和实际运行测试。
- 不启动 PIE、不模拟输入、不自动运行游戏场景或自动化测试。

## 验证记录

2026-08-26 完成代码与编译验证；不代表运行时玩法验收。

- UBT：`Build.bat SekiroEditor Win64 Development -Project=F:/ProjectAI/Sekiro/Sekiro.uproject -WaitMutex -Module=Sekiro`，退出码 0。
  最终增量构建耗时 10.12 秒，日志见 `Script/temp/gas-numeric-foundation-ubt.log`；首轮日志见同目录 `gas-numeric-foundation-ubt-initial.log`。
- 新增 5 组 `Sekiro.GAS.Numeric.*` 契约测试（初始化、资源、上限回落、重入、护甲曲线），已编入 SekiroEditor，未执行。
- 3 个变更 Lua 文件通过 Lua 5.4.3 `luaL_loadfilex` 语法编译，仅加载编译、不执行脚本。
- Lua 函数文档检查：537/537 完整，0 问题；相关 C++ 源码 UTF-8 BOM 与差异检查通过。
- 现有 Lua 不再调用 SetMaxPosture/SetCurrentPosture；文档相对链接检查通过。
- 未启动 PIE、未模拟玩家输入、未执行自动化测试或其他运行时功能测试。

持有效果接口仅支持设计规定的非周期统计属性增益；原生周期资源 GE、网络预测/复制与统一命中协议仍留待后续任务。
SurvivalComponent、死亡/回生门禁和全量躯干参数已由生存组件子任务实现；本节以上是 GAS 初始阶段的验证记录，不代表后续扩展运行验收。

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-08-26 | 用户确认从父需求拆出 GAS 数值子任务，并要求设计与实现；创建计划，开始实施 |
| 2026-08-26 | 完成 GAS 属性/效果接口、角色接入和 Lua 架势迁移；补齐 5 组契约测试代码，通过 UBT 与静态检查，未执行运行测试 |
| 2026-08-26 | 关联后续 USKVitalsComponent 设计，生命/躯干状态及 ASC 门禁扩展待实现；本任务状态仍为代码完成、仅编译验证 |
| 2026-08-26 | 用户确认 Survival 名称；同步后续任务链接与全量躯干属性/逻辑归属，保留本任务已有验证状态 |
| 2026-08-26 | 生存组件子任务落地全量 30 字段初始化、ASC 单一资源策略与复合资源提交；同步当前接口文档，扩展验证记录归生存组件子任务 |
| 2026-08-26 | 主角与 AI 各自独立 Lua 属性文件，包含 Health+Combat 全量初值；移除角色 Profile 和共享默认表，UBT 与 Lua 静态检查通过，未执行运行测试 |
| 2026-08-26 | 生命和 Combat 属性统一为 Character：一个原生属性集、一张平铺角色 Lua 表；合并验证记录见生存组件子任务 |
