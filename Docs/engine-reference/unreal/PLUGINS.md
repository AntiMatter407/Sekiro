# Unreal Engine 5.7 — 可选插件与系统

**最后验证：** 2026-02-13

本文档索引了 Unreal Engine 5.7 中可用的**可选插件和系统**。这些不属于核心引擎，但常用于特定类型的游戏。

---

## 如何使用本指南

**✅ 有详细文档** — 参见 `plugins/` 目录获取完整指南
**🟡 仅有简要概述** — 链接至官方文档，需使用 WebSearch 获取详情
**⚠️ 实验性** — 未来版本可能有破坏性变更
**📦 需要插件** — 必须在 `Edit > Plugins` 中启用

---

## 生产就绪系统（有详细文档）

### ✅ Gameplay Ability System (GAS)
- **用途：** 模块化技能系统（技能、属性、效果、冷却时间、消耗）
- **适用场景：** RPG、MOBA、带技能的射击游戏、任何基于技能的玩法
- **知识缺口：** GAS 自 UE4 起稳定，UE5 改进在知识截止之后
- **状态：** 生产就绪
- **插件：** `GameplayAbilities`（内置，在 Plugins 中启用）
- **详细文档：** [plugins/gameplay-ability-system.md](plugins/gameplay-ability-system.md)
- **官方文档：** https://docs.unrealengine.com/5.7/en-US/gameplay-ability-system-for-unreal-engine/

---

### ✅ CommonUI
- **用途：** 跨平台 UI 框架（自动手柄/鼠标/触控输入路由）
- **适用场景：** 多平台游戏（主机 + PC）、输入无关的 UI
- **知识缺口：** UE5+ 生产就绪，重大改进在知识截止之后
- **状态：** 生产就绪
- **插件：** `CommonUI`（内置，在 Plugins 中启用）
- **详细文档：** [plugins/common-ui.md](plugins/common-ui.md)
- **官方文档：** https://docs.unrealengine.com/5.7/en-US/commonui-plugin-for-advanced-user-interfaces-in-unreal-engine/

---

### ✅ Gameplay Camera System（游戏相机系统）
- **用途：** 模块化相机管理（相机模式、混合、上下文感知相机）
- **适用场景：** 需要动态相机行为的游戏（第三人称、瞄准、载具）
- **知识缺口：** UE 5.5 新增，完全在知识截止之后
- **状态：** ⚠️ 实验性（UE 5.5-5.7）
- **插件：** `GameplayCameras`（内置，在 Plugins 中启用）
- **详细文档：** [plugins/gameplay-camera-system.md](plugins/gameplay-camera-system.md)
- **官方文档：** https://docs.unrealengine.com/5.7/en-US/gameplay-cameras-in-unreal-engine/

---

### ✅ PCG（程序化内容生成，Procedural Content Generation）
- **用途：** 基于节点的程序化世界生成（植被、道具、地形细节）
- **适用场景：** 开放世界、程序化关卡、大规模环境填充
- **知识缺口：** UE 5.0-5.6 实验性，5.7 生产就绪
- **状态：** 生产就绪（截至 UE 5.7）
- **插件：** `PCG`（内置，在 Plugins 中启用）
- **详细文档：** [plugins/pcg.md](plugins/pcg.md)
- **官方文档：** https://docs.unrealengine.com/5.7/en-US/procedural-content-generation-in-unreal-engine/

---

## 其他生产就绪插件（简要概述）

### 🟡 Mass Entity
- **用途：** 高性能 ECS，用于大规模 AI/人群（10,000+ 实体）
- **适用场景：** RTS、城市模拟、大规模人群、大规模 AI
- **状态：** 生产就绪（UE 5.1+）
- **插件：** `MassEntity`、`MassGameplay`、`MassCrowd`
- **官方文档：** https://docs.unrealengine.com/5.7/en-US/mass-entity-in-unreal-engine/

---

### 🟡 Niagara Fluids（Niagara 流体）
- **用途：** GPU 流体模拟（烟雾、火焰、液体）
- **适用场景：** 真实火焰/烟雾效果、水模拟
- **状态：** 实验性 → 生产就绪（UE 5.4+）
- **插件：** `NiagaraFluids`（内置）
- **官方文档：** https://docs.unrealengine.com/5.7/en-US/niagara-fluids-in-unreal-engine/

---

### 🟡 Water Plugin（水系统插件）
- **用途：** 海洋、河流、湖泊渲染及浮力
- **适用场景：** 有水体的游戏、船只、游泳
- **状态：** 生产就绪（UE 5.0+）
- **插件：** `Water`（内置）
- **官方文档：** https://docs.unrealengine.com/5.7/en-US/water-system-in-unreal-engine/

---

### 🟡 Landmass Plugin（地形插件）
- **用途：** 地形雕刻和场景编辑
- **适用场景：** 大规模地形修改、程序化场景
- **状态：** 生产就绪
- **插件：** `Landmass`（内置）
- **官方文档：** https://docs.unrealengine.com/5.7/en-US/landmass-plugin-in-unreal-engine/

---

### 🟡 Chaos Destruction（Chaos 破坏系统）
- **用途：** 实时破碎和破坏效果
- **适用场景：** 可破坏环境（墙壁、建筑、物体）
- **状态：** 生产就绪（UE 5.0+）
- **插件：** `ChaosDestruction`（内置）
- **官方文档：** https://docs.unrealengine.com/5.7/en-US/destruction-in-unreal-engine/

---

### 🟡 Chaos Vehicle（Chaos 载具系统）
- **用途：** 高级载具物理（轮式载具、悬挂系统）
- **适用场景：** 赛车游戏、重度载具玩法
- **状态：** 生产就绪（替代 PhysX Vehicles）
- **插件：** `ChaosVehicles`（内置）
- **官方文档：** https://docs.unrealengine.com/5.7/en-US/chaos-vehicles-overview-in-unreal-engine/

---

### 🟡 Geometry Scripting（几何体脚本）
- **用途：** 运行时程序化网格体生成与编辑
- **适用场景：** 动态网格体创建、程序化建模
- **状态：** 生产就绪（UE 5.1+）
- **插件：** `GeometryScripting`（内置）
- **官方文档：** https://docs.unrealengine.com/5.7/en-US/geometry-scripting-in-unreal-engine/

---

### 🟡 Motion Design Tools（运动设计工具）
- **用途：** 运动图形、程序化动画、关键帧动画
- **适用场景：** UI 动画、程序化运动、关键帧序列
- **状态：** 实验性 → 生产就绪（UE 5.4+）
- **插件：** `MotionDesign`（内置）
- **官方文档：** https://docs.unrealengine.com/5.7/en-US/motion-design-mode-in-unreal-engine/

---

## 实验性插件（谨慎使用）

### ⚠️ AI Assistant（UE 5.7+）
- **用途：** 编辑器内 AI 指导和帮助
- **状态：** 实验性
- **插件：** 在 UE 5.7 设置中启用
- **官方文档：** 在 UE 5.7 发行公告中宣布

---

### ⚠️ OpenXR（VR/AR）
- **用途：** 跨平台 VR/AR 支持
- **适用场景：** VR/AR 游戏
- **状态：** VR 生产就绪，AR 实验性
- **插件：** `OpenXR`（内置）
- **官方文档：** https://docs.unrealengine.com/5.7/en-US/openxr-in-unreal-engine/

---

### ⚠️ Online Subsystem（EOS、Steam 等）
- **用途：** 平台无关的在线服务（匹配、好友、成就）
- **适用场景：** 带在线功能的多人游戏
- **状态：** 生产就绪
- **插件：** `OnlineSubsystem`、`OnlineSubsystemEOS`、`OnlineSubsystemSteam`
- **官方文档：** https://docs.unrealengine.com/5.7/en-US/online-subsystem-in-unreal-engine/

---

## 已弃用插件（新项目避免使用）

### ❌ PhysX Vehicles
- **已弃用：** 请改用 Chaos Vehicles
- **状态：** 旧版，不推荐

---

### ❌ 旧版复制图 (Old Replication Graph)
- **已弃用：** 已被 Iris 替代（UE 5.1+）
- **状态：** 请使用 Iris 进行现代网络通信

---

## 按需 WebSearch 策略

当用户询问上述未列出的插件时，请使用以下方法：

1. **WebSearch** 查找最新文档：`"Unreal Engine 5.7 [插件名称]"`
2. 验证该插件是否：
   - 在知识截止之后（2025 年 5 月训练数据之后）
   - 实验性还是生产就绪
   - 在 UE 5.7 中仍然受支持
3. 可选择将发现缓存到 `plugins/[插件名称].md` 以供将来参考

---

## 快速决策指南

**我需要技能/法术/增益** → **Gameplay Ability System (GAS)**
**我需要跨平台 UI（主机 + PC）** → **CommonUI**
**我需要动态相机** → **Gameplay Camera System**
**我需要程序化世界** → **PCG**
**我需要大规模人群（数千 AI）** → **Mass Entity**
**我需要可破坏环境** → **Chaos Destruction**
**我需要载具** → **Chaos Vehicles**
**我需要水体/海洋** → **Water Plugin**
**我需要 VR/AR** → **OpenXR**

---

**最后更新：** 2026-02-13
**引擎版本：** Unreal Engine 5.7
**LLM 知识截止日期：** 2025 年 5 月
