# 只狼动画+输入系统复刻（直接播放方案）

| 状态 | 创建 | 更新 |
|------|------|------|
| 🔄 进行中 | 2026-06-15 | 2026-06-17 |

## 需求描述

将只狼的动画系统和输入系统 1:1 复刻到 UE5 中，采用**直接动画播放**方式（Montage），不构建复杂的 UE 动画蓝图状态机。

**核心原则**：忠实复刻 FromSoftware 原版 TAE（Time Action Event）系统的逻辑——动画内嵌事件驱动的"推送"架构，三层优先级状态机（Locomotion → Combat → Reaction），取消窗口帧级判定。

与旧方案 `sekiro-anim-blueprint.md`（AnimBP 状态机方案）的区别：放弃状态机驱动的 Transition Rule，改为按键→意图→Montage 直线映射，由新组件统一管理。

**已有基础**：1426 个动画资产、17 个 Enhanced Input Action、IMC_Sekiro、SKCharacter + SKMovementComponent + SKAnimInstance、Sekiro_TAE_Logic.json（2209 动画，60000+ TAE 事件）。

## 架构

```
Extracted/Sekiro_TAE_Logic.json (512KB)
        │
        ▼  SekiroTAEImporter（编辑器管线）
        │
┌───────────────────────────────┐
│  USKAnimLogicData (DataAsset) │  ← TAE 数据抽象为 UE 资产
│  · CancelRules                │
│  · JumpTableFlags（帧级）      │
│  · AttackHitboxConfigs        │
│  · SpEffectConfigs            │
└───────────────┬───────────────┘
                │  运行时加载
Enhanced Input  │
      │         │
      ▼         ▼
┌──────────────────────┐
│  USKInputHandler     │  ← 输入→意图
└─────────┬────────────┘
          │ 动作意图
          ▼
┌──────────────────────┐
│ USKAnimationController│ ← 意图→动画
│  · 三层优先级状态机   │
│  · CancelWindow 判定  │
│  · Montage 播放       │
│  · 行为标志注入       │
└─────────┬────────────┘
          │
          ▼
  AnimInstance / SkeletalMesh
```

## 任务树

- ✅ 1. 新建输入处理组件（依赖: 无）
  - ✅ 1.1 创建 USKInputHandler 组件（挂载到 ASKCharacter）
  - ✅ 1.2 输入事件从 ASKCharacter 迁移到 USKInputHandler
  - ✅ 1.3 实现输入状态机（按键→动作意图映射，含长按/组合键判定）
  - ✅ 1.4 暴露动作意图接口（MoveIntent / AttackIntent / GuardIntent / DodgeIntent 等）
  - ✅ 1.5 验证 IMC_Sekiro 全部 17 个 Action 按键绑定

- ✅ 2. TAE 数据 → UE DataAsset（依赖: 无）
  - ✅ 2.1 完善 USKAnimLogicData DataAsset 结构（FSKFrameFlags 16位 + FSKAnimFrameData 关键帧 + FSKAttackHitboxList 多盒）
  - ✅ 2.2 实现 TAE JSON → DataAsset 管线（FSekiroAnimDataBuilder + USekiroImportLibrary 封装）
  - ✅ 2.3 补充未映射的 JumpTable ID（15+ 个高频 ID：133/134/32/31/26/137/154/51/28/11/55 等）
  - ✅ 2.4 验证 DataAsset 覆盖全部 2209 个动画的 TAE 事件（build_anim_data.py 管线脚本）

- ✅ 3. 新建动画控制组件（依赖: 1, 2）
  - ✅ 3.1 创建 USKAnimationController 组件骨架（挂载到 ASKCharacter）
  - ✅ 3.2 加载并缓存 USKAnimLogicData DataAsset
  - ✅ 3.3 实现三层优先级状态机（Locomotion / Combat / Reaction，对齐原版 0/1-7/8-10）
  - ✅ 3.4 实现 CancelWindow 帧级判定（30fps 查询 DataAsset CancelRules）
  - ✅ 3.5 实现 Montage 播放（Crossfade 过渡 + JumpTable 行为标志注入）
  - ✅ 3.6 消费 USKInputHandler 意图，驱动动画选择

- ✅ 4. 基础移动（依赖: 3）
  - ✅ 4.1 SKAnimInstance 清理 BlendSpace（移除 CachedBlendSpacePlayer）
  - ✅ 4.2 USKAnimationController 新增 ProcessLocomotion（五级速度+八方向）
  - ✅ 4.3 过渡动画衔接（Tier升降+Stop→Idle）
  - ✅ 4.4 Turn 转身（45°/90°/135°/180°）

- 🔄 5. 攻击系统（依赖: 3）
  - ✅ 5.1 攻击基础组件（补充 USKAnimationController 攻击接口）
    - ✅ 5.1.1 新增攻击状态追踪（Combo链/蓄力等级/攻击变体选择器）
    - ✅ 5.1.2 实现 R1 连段 Combo01-04 的条件推进逻辑（含超时复位）
    - ✅ 5.1.3 蓄力攻击判定（长按>0.3s → Charged / Charged_Step / Charged_Dash / Charged_L）
    - ✅ 5.1.4 移动方向→攻击变体映射（Dodge攻击/Quickstep攻击/Slide攻击/Sprint攻击）
    - ✅ 5.1.5 空中攻击 + 蹲行攻击
  - ⬜ 5.2 攻击盒（Hitbox）激活系统（依赖: 5.1）
    - ⬜ 5.2.1 实现 Hitbox 激活 Notify（从 DataAsset AttackHitboxConfigs 读取）
    - ⬜ 5.2.2 攻击盒碰撞检测与伤害触发
  - ⬜ 5.3 攻击动画资产绑定与验证（依赖: 5.1）
    - ⬜ 5.3.1 验证 CategoryAnimMap 中攻击类别覆盖全部 80+ 攻击动画
    - ⬜ 5.3.2 验证 CancelWindow 数据覆盖攻击连段

- ⬜ 6. 防御系统（依赖: 3）
  - ⬜ 6.1 Guard（举起/放下/维持待机/受击）
  - ⬜ 6.2 Deflect 弹刀（精确时机 + 反击）
  - ⬜ 6.3 Guard Break 架势崩 + Guard Counter 反击

- ⬜ 7. 闪避系统（依赖: 3）
  - ⬜ 7.1 Dodge 四方向 + Quickstep 四方向

- ⬜ 8. 跳跃系统（依赖: 3）
  - ⬜ 8.1 Jump 起跳 / 空中下落 / 落地 + 方向变体

- ⬜ 9. 受击与死亡（依赖: 3）
  - ⬜ 9.1 受击反应（轻/中/重/击退/头部/毒/恐怖）
  - ⬜ 9.2 死亡（6方向 + 坠落/抓取）+ 回生

- ⬜ 10. 特殊动作（依赖: 3）
  - ⬜ 10.1 Grapple 钩索（起钩→飞行→落地）
  - ⬜ 10.2 Deathblow 忍杀（正面/背面/空中/潜行）
  - ⬜ 10.3 CombatArt 武技（一字斩/不死斩/旋风斩 等）
  - ⬜ 10.4 Prosthetic 义手忍具
  - ⬜ 10.5 ItemUse 道具使用

- ⬜ 11. 动画事件注入（依赖: 5-10）
  - ⬜ 11.1 AttackHitbox / CancelWindow / 行为标志 Notify

- ⬜ 12. 集成测试（依赖: 全部）
  - ⬜ 12.1 PIE 全按键功能 + 过渡流畅性 + 优先级打断验证
  - ⬜ 12.2 AIBridge 输入模拟工具（依赖: 12.1）
    - ⬜ 12.2.1 创建 USKInputSimulateTool（C++，SekiroAIBridge 插件侧）
      - 工具名: `input.simulate`
      - 参数: `action`（动作名，如 `attack`/`move`/`guard`/`dodge`/`jump`/`interact`...）
      - 参数: `value`（可选，`bool` 或 `FVector2D`，如 `{"x":1,"y":0}` 表示右移）
      - 参数: `hold_time`（可选，长按持续时间秒，0 表示一次触发）
      - 参数: `delay`（可选，操作前延迟秒）
      - 功能：在 PIE 世界中找到玩家角色的 USKInputHandler，直接调用其内部回调方法模拟输入
    - ⬜ 12.2.2 在 bridge.py 中添加 `input_simulate` 命令
    - ⬜ 12.2.3 编写测试用例脚本（攻击连段、移动+攻击组合、防御+反击）

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-15 | 创建文档，双组件架构（USKInputHandler + USKAnimationController） |
| 2026-06-15 | Task 1 🔄 进行中，技术方案写入 tech-designs/sekiro-anim-input-replica-1.md |
| 2026-06-15 | Task 1 ✅ 完成：USKInputHandler 组件（Input/目录）+ SKCharacter 瘦身 |
| 2026-06-15 | 重构任务树：新增 Task 2（TAE→DataAsset），基于原版 TAE 系统分析（三层状态机/CancelWindow/JumpTable）；Task 3 USKAnimationController 细化；后续任务顺延+1 |
| 2026-06-15 | Task 2 🔄 进行中，技术方案写入 tech-designs/sekiro-anim-input-replica-2.md |
| 2026-06-15 | Task 2 ✅ 完成：FSKFrameFlags(16位) + FSKAnimFrameData + FSKAttackHitboxList + FSekiroAnimDataBuilder + 15+ JumpTable ID + build_anim_data.py |
| 2026-06-15 | Task 3 🔄 进行中，技术方案写入 tech-designs/sekiro-anim-input-replica-3.md |
| 2026-06-15 | Task 3 ✅ 完成：USKAnimationController（优先级状态机 + CancelWindow判定 + Montage播放 + FrameFlags注入） |
| 2026-06-17 | 插入 Task 12.2 AIBridge 输入模拟工具，用于 PIE 下自动化测试动画与输入联动 |
| 2026-06-17 | Task 5 递归拆分：三个子任务（基础组件 + Hitbox系统 + 资产验证），5.1 含五个叶子任务 |
| 2026-06-17 | Task 5.1 ✅ 完成（HandleAttack/GetComboAnimID/GetMoveDirectionSuffix/ResetAttackState/CheckChargeRelease实现+编译通过）；修复 SekiroImport 插件 Bit_* 常量重复定义编译错误 |
