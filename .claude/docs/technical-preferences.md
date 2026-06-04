# 技术偏好

<!-- 由 /setup-engine 填充。随用户在开发过程中的决策而更新。 -->
<!-- 所有代理参考此文件以获取项目特定的标准和约定。 -->

## 引擎与语言

- **Engine（引擎）**: Unreal Engine 5.2
- **Language（语言）**: C++（主要），Blueprint + Lua（脚本层）
- **Rendering（渲染）**: [待配置]
- **Physics（物理）**: [待配置]

## 命名约定（Naming Conventions）

以下规则仅适用于项目 C++ 代码，UE 引擎源码不受此约束。

### 类名前缀

UE 标准前缀 + SK 项目前缀组合：

| 前缀 | 基类 | 示例 |
|------|------|------|
| `ASK` | AActor | `ASKPlayerCharacter` |
| `USK` | UObject | `USKHealthComponent` |
| `FSK` | 结构体 | `FSKDamageInfo` |
| `ISK` | 接口 | `ISKDamageable` |
| `ESK` | 枚举 | `ESKStanceType` |
| `TSK` | 模板类 | `TSKObjectPool` |

### 文件命名

- **C++ 文件**：强制使用 SK 前缀，与类名匹配 —— `SKPlayerController.h`、`SKHealthComponent.cpp`
- **文件夹**：不加前缀 —— `Player/`、`Combat/`、`UI/`

### 变量与函数

- **变量**：PascalCase（如 `MoveSpeed`）
- **布尔值**：`b` 前缀（如 `bIsAlive`、`bCanAttack`）
- **函数**：PascalCase（如 `TakeDamage()`）
- **常量**：PascalCase 或 UPPER_SNAKE_CASE
- **信号/事件**：PascalCase 过去时（如 `OnHealthChanged`、`OnDeath`）

## 性能预算（Performance Budgets）

- **Target Framerate（目标帧率）**: [待配置] — 建议：60fps / 16.6ms 帧预算
- **Frame Budget（帧预算）**: [待配置]
- **Draw Calls（绘制调用）**: [待配置]
- **Memory Ceiling（内存上限）**: [待配置]

## 测试

- **Framework（框架）**: [待配置] — 建议：UE Automation Testing Framework
- **Minimum Coverage（最低覆盖率）**: [待配置]
- **Required Tests（必需测试）**: 平衡公式、游戏系统、网络（如适用）

## 禁用模式（Forbidden Patterns）

<!-- 添加不应出现在本项目代码库中的模式 -->
- [尚未配置 — 在做出架构决策时添加]

## 允许的库 / 插件（Allowed Libraries / Addons）

<!-- 在此添加已批准的第三方依赖 -->
- [尚未配置 — 在批准依赖后添加]

## 架构决策日志（Architecture Decisions Log）

<!-- 快速参考，链接至 Docs/architecture/ 中的完整 ADR -->
- [尚无 ADR — 使用 /architecture-decision 创建]
