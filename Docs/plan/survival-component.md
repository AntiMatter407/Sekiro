# 生存组件

| 状态 | 创建 | 更新 |
|------|------|------|
| ✅ 已实现（编译与静态验证） | 2026-08-26 | 2026-08-26 |

用户确认组件名称：**USKSurvivalComponent**。本设计替代原生命组件方案，统一处理生命、死亡、回生和全部躯干逻辑。
父任务：[Boss 对战基础闭环](boss-player-combat-foundation.md) 2.2–2.5。
依赖：[GAS 数值基础](gas-numeric-foundation.md)。详细方案：[生存组件](../design/survival-component.md)。
标签来源：[Gameplay C++ 插件独立子需求](lua-gameplay-tags.md) 将生成来源迭代为原生标签 DataTable；Survival 保持七个标签名、字典查询和绑定前完整校验，迁移结果见该子需求。

## 范围

统一生命/躯干查询、伤害与恢复请求、死亡和回生状态、躯干崩溃/恢复、状态门禁及事件。
生命与躯干属性继续由继承 UAttributeSet 的现有属性集唯一保存；不增加 MaxHealth 等组件数值配置。
全部躯干数值（增长值/倍率/封顶、恢复速度/延迟/渐进参数、崩溃时长、恢复/回生躯干比例及崩溃混合时间）均进入 AttributeSet。
全部躯干计算与流程进入 SurvivalComponent/对应 Lua；Combat 只裁决攻防结果与提供通用动作接口。
不实现完整统一命中、Boss 阶段、回生 UI 或网络复制；回生费用与具体演出由外部项目规则负责。
本轮完成数值迁移、原生组件、Lua 工作流及角色接入；运行验收尚未执行。当前没有死亡/回生专用动画、费用或 UI，死亡下一安全 Tick 收尾，回生由外部显式调用。

## 任务树

- ✅ 1. 方案设计（父任务 2.2）
  - ✅ 1.1 命名、GAS / Survival / Combat / Lua 所有权划分
  - ✅ 1.2 生命与躯干两组状态、序号、事件和死亡优先规则
  - ✅ 1.3 ASC 资源策略、提交边界和回调重入约束
  - ✅ 1.4 回生 Begin/Complete/Cancel、恢复失败和旧回调处理
  - ✅ 1.5 初始化、崩溃迁移、实现文件与验收范围
  - ✅ 1.6 用户确认 Survival 命名，补齐全量躯干属性及逻辑迁移契约
- ✅ 2. 原生通用实现（父任务 2.3，依赖 1）
  - ✅ 2.1 ASC 原生资源策略、缺失策略门禁与明确拒绝原因
  - ✅ 2.2 复合资源结算及内部回生/崩溃资源 GE
  - ✅ 2.3 SurvivalComponent、类型、状态转换和事件
  - ✅ 2.4 Character 挂载、初始化与解绑，补齐纯原生组件的 Lua BeginPlay/EndPlay 分发
  - ✅ 2.5 Combat 崩溃状态迁出、只读兼容与令牌演出入口
  - ✅ 2.6 全量躯干 AttributeSet 字段、初始化结构/Profile、属性校验、快照及 GE 支持
- ✅ 3. Lua 接入（父任务 2.4，依赖 2）
  - ✅ 3.1 全量默认属性初始化移至 Survival Lua，清理 CombatConfig.Posture 数值副本
  - ✅ 3.2 迁移躯干增长/封顶、自然恢复、崩溃清空/恢复与动画响应，全部参数读取 GAS
  - ✅ 3.3 玩家/AI 生命周期清理、按原因输入锁及原有 AI 恢复；无死亡动画时下一安全 Tick 收尾
  - ✅ 3.4 费用/资格外部接入边界，显式 Begin/Complete/Cancel 回生及失败保持门禁；不自动扣费或完成回生
- ✅ 4. 契约与验证（父任务 2.5，依赖 3）
  - ✅ 4.1 添加 7 组生存契约测试，扩展 GAS 参数校验与代表性新增参数 GE 添加/移除测试；全部初始化字段静态核对
  - ✅ 4.2 UBT 编译对应目标、Lua 语法与文档静态检查
  - ✅ 4.3 同步实现与验证记录；运行测试另待用户明确要求
- ✅ 5. 角色专属 Lua 属性配置
  - ✅ 5.1 主角与通用 AI 各自一份完整 Character 属性表，不共享数值默认表
  - ✅ 5.2 Character 暴露 AttributeConfigModule；Lua 选择配置、严格校验并初始化统一属性集
  - ✅ 5.3 移除角色 AttributeProfile 优先路径与 AttributeDefaults.lua，保留 ASC 独立兼容 API
  - ✅ 5.4 UBT、Lua 语法/函数文档、30 字段对齐和编码静态检查
- ✅ 6. 统一 Character 属性模型
  - ✅ 6.1 合并为 USKCharacterAttributeSet，移除旧 Health/Combat 两个属性集及默认子对象
  - ✅ 6.2 ASC、数值 GE、Combat 查询及既有契约测试迁移到同一属性集
  - ✅ 6.3 主角/AI 配置平铺为 Character 表，取消 Health/Combat 分组，全部数值保持不变
  - ✅ 6.4 单集注册、完整初始化与快照契约测试编译，UBT 及静态检查通过

## 风险与约束

| 风险 | 约束 |
|------|------|
| 死亡后可绕过组件直接调用 ASC 治疗 | 策略校验落在 ASC 支持的全部资源入口 |
| 在 GAS 通知中重置躯干/回生 | 通知只消费事实；后续写入移出调用栈并重新检查令牌 |
| Combat 与 Survival 都保存 Broken | 迁出唯一状态权威，兼容接口只读转发 |
| Lua 残留躯干配置导致 GE 参数不生效 | 全部可调数值迁至 AttributeSet，计算时读当前属性 |
| 生命和躯干分两笔写入产生相反演出 | 同笔复合请求结束后统一评估，死亡优先 |
| 动画结束回调落在新生命轮次 | 所有流程回调校验组件身份与序号 |
| 恢复资源失败却解锁输入 | 先验证 GE 实际结果，成功才完成状态转换 |

## 验证记录

2026-08-26 完成以下编译和静态验证，不代表运行时行为或手感验收：

- UBT：`SekiroEditor Win64 Development -Project=F:/ProjectAI/Sekiro/Sekiro.uproject -WaitMutex -Module=Sekiro`，退出码 0；最终独立复核输出 `Target is up to date`（1.22 秒）。
- 原生实现最终编译/链接成功（11.68 秒），日志：[实现编译](../../Script/temp/survival-component-ubt-agent.log)；独立复核：[最终编译](../../Script/temp/survival-component-ubt.log)。
- 新增 `Sekiro.Survival.*` 7 组测试：初始化顺序、初始状态、死亡优先、躯干恢复、回生令牌、同步重入、上限变化。测试代码已纳入模块编译，未执行。
- 扩展既有 5 组 GAS 测试中的初始化边界、恢复延迟/封顶参数持续 GE 添加及移除断言；未执行。
- 4 个变更 Lua 文件经项目 Lua 5.4.3 `luaL_loadfilex` 语法编译通过，未执行脚本。
- Lua 中文函数文档检查：562/562 完整，0 项问题。
- 30 个初始化字段与 Lua 默认表、属性快照逐项匹配；相关 21 个 C++ 文件 UTF-8 BOM 检查通过。
- 6 个相关设计/计划文档相对链接与行尾空白检查、修改范围 `git diff --check` 通过。
- 未启动 UE 编辑器/PIE，未执行自动化测试、输入模拟或其他运行测试，未修改二进制资产。

后续运行验收仍需检查委托事件次数、玩家/AI 的实际输入与动画恢复、动态 GE 参数对自然恢复曲线的影响，以及失败/取消/离场流程。完整命中扣血仍由父任务 3 接入。

### 角色 Lua 配置迭代验证

- UBT 同目标/模块退出码 0（24.33 秒），包括 Character UHT 生成与模块链接；[编译日志](../../Script/temp/attribute-lua-config-ubt.log)。UnLua 依赖编译有既有弃用 API 警告，本轮未修改插件源码。
- 新增 CharacterAttributes.lua、主角 Attributes.lua、AI Attributes.lua，以及变更的 Survival Lua 均通过 Lua 5.4.3 语法编译；没有执行 Lua chunk。
- Lua 函数文档 564/564 完整，0 项问题；两份独立配置各 30 个字段与原生初始化结构、加载器字段契约一致。
- 本轮两个 C++ 文件保持 UTF-8 BOM；Lua 为 UTF-8 无 BOM，缩进与行尾空白检查通过。
- 未启动编辑器/PIE，未执行自动化或运行测试，未改二进制资产。其他 AI 通过 AttributeConfigModule 显式指定独立配置文件；现有主角/通用 AI 初值未调整。

### 统一 Character 迭代验证（2026-08-26）

- UBT 编译与链接退出码 0（26.34 秒），[完整编译日志](../../Script/temp/character-attribute-set-ubt-agent.log)；根代理独立复核退出码 0、目标最新（1.29 秒），[复核日志](../../Script/temp/character-attribute-set-ubt.log)。
- 4 个 Lua 文件语法检查通过，函数文档 564/564 完整；两份角色表各 30 个字段与原生初始化结构及加载器完全匹配，逐项表达式比较确认数值未改变。
- 原生统一属性集包含 30 个常驻属性及 4 个临时属性，非资源配置属性共 28 个且无重复；Character 只创建/注册一个属性集。
- Source 内旧属性类引用为零，旧 4 个源码文件已删除；新类及修改文件保持 UTF-8 BOM。测试仅编译，没有执行自动化、Lua 工作流或 PIE。
- 未改二进制资产；旧蓝图/GE 若保存了旧属性类路径，需改选统一 Character 属性后重新编译，不能将本次源码编译视为资产兼容验收。

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-08-26 | 建立 USKVitalsComponent 设计子任务，覆盖生命、死亡、回生和躯干，待后续实施 |
| 2026-08-26 | 用户确认 USKSurvivalComponent；重命名任务/设计文件并修订全部躯干参数和逻辑归属，代码任务保持待实现 |
| 2026-08-26 | 用户要求实施；完成 Survival 原生/Lua、全量 GAS 躯干参数、角色与 Combat 门禁、7 组契约测试代码及编译/静态检查；运行验收未执行 |
| 2026-08-26 | 用户要求每种角色一份 Lua 属性表；完成 Health/Combat 同文件配置、角色模块选择与严格加载，保留 GAS 数值权威和初始化幂等 |
| 2026-08-26 | 按用户要求合并为 Character：一个原生 AttributeSet 和每角色一张平铺 Lua 表；迁移 GAS/GE/测试引用，编译及静态检查通过，未执行运行验收 |
