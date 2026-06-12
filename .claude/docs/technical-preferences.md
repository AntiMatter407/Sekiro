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

## C++/脚本分工原则

这是本项目最核心的架构原则，所有 C++ 代码和脚本代码都必须遵守：

1. **C++ 层只提供通用接口** — `UFUNCTION(BlueprintCallable)` 或 Commandlet 入口函数，每个接口做一件事（创建 BlendSpace、分配 ABP、发现角色蓝图等），不硬编码任何项目特定路径、名称或参数
2. **脚本层负责编排工作流** — Python/Lua/Blueprint 脚本调用 C++ 接口，传入项目特定的参数（路径、类型、JSON 配置等），实现具体业务逻辑
3. **禁止在 C++ 中硬编码特定工作流** — Commandlet 中不得出现项目特定的默认路径、类名过滤器、样本配置等；这些应通过命令行参数传入，或在脚本层指定
4. **脚本文件统一放在 `Script/` 目录** — 所有项目自有 Python 脚本（Blender、UE5、诊断/验证）统一放在 `Script/` 目录下；`Tools/` 目录仅存放第三方工具和 C# 工具链

### 正确示例

```cpp
// C++ 提供通用接口
UFUNCTION(BlueprintCallable)
static UBlendSpace* CreateBlendSpace2D(
    USkeleton* Skeleton,
    const TArray<FBlendSpaceSample>& Samples,
    const FString& OutputPath
);
```

```python
# Python 脚本编排 Sekiro 特定工作流
samples = load_json("sekiro_locomotion_samples.json")
skeleton = find_skeleton("/Game/Characters/Sekiro")
bs = CreateBlendSpace2D(skeleton, samples, "/Game/Characters/Sekiro/BS_Locomotion")
abp = CreateAnimBlueprint(skeleton, "USekiroAnimInstance", bs, "/Game/Characters/Sekiro/ABP_Character")
AssignABPToCharacter(abp, "/Game/Gameplay/BP_SekiroCharacter")
```

### 错误示例

```cpp
// 违规：C++ 中硬编码 Sekiro 特定逻辑
FString OutputPath = TEXT("/Game/Characters/Sekiro");  // 项目特定路径
ParentClass = TEXT("USekiroAnimInstance");              // 项目特定类名
if (BP->ParentClass->IsChildOf(ASekiroCharacter::StaticClass()))  // 项目特定类型过滤
```

## 禁用模式（Forbidden Patterns）

<!-- 添加不应出现在本项目代码库中的模式 -->
- **C++ 中硬编码具体工作流** — 见上方 C++/脚本分工原则
- [其他模式待配置 — 在做出架构决策时添加]

## 允许的库 / 插件（Allowed Libraries / Addons）

<!-- 在此添加已批准的第三方依赖 -->
- [尚未配置 — 在批准依赖后添加]

## 架构决策日志（Architecture Decisions Log）

<!-- 快速参考，链接至 Docs/architecture/ 中的完整 ADR -->
- [尚无 ADR — 使用 /architecture-decision 创建]
