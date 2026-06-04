# Unreal Engine 5.7 — Gameplay Camera System（游戏相机系统）

**最后验证：** 2026-02-13
**状态：** ⚠️ 实验性（UE 5.5 引入）
**插件：** `GameplayCameras`（内置，在 Plugins 中启用）

---

## 概述

**Gameplay Camera System** 是 UE 5.5 引入的模块化相机管理框架。它用灵活的基于节点的系统取代了传统相机设置，处理相机模式、混合和上下文感知的相机行为。

**适用场景：**
- 动态相机行为（第三人称、瞄准、载具、电影级）
- 上下文感知的相机切换（战斗、探索、对话）
- 相机模式之间的平滑混合
- 程序化相机运动（相机震动、延迟、偏移）

**警告：** 此插件在 UE 5.5-5.7 中为实验性。未来版本可能有 API 变更。

---

## 核心概念

### 1. **Camera Rig**（相机装备）
- 定义相机配置（位置、旋转、FOV 等）
- 模块化节点图（类似材质编辑器）

### 2. **Camera Director**（相机导演）
- 管理哪个 Camera Rig 处于活动状态
- 处理 Camera Rig 之间的混合

### 3. **Camera Nodes**（相机节点）
- 相机行为的构建模块：
  - **位置节点**：环绕、跟随、固定位置
  - **旋转节点**：注视、匹配 Actor 旋转
  - **修饰器**：相机震动、延迟、偏移

---

## 设置

### 1. 启用插件

`Edit > Plugins > Gameplay Cameras > Enabled > Restart`

### 2. 添加相机组件

```cpp
#include "GameplayCameras/Public/GameplayCameraComponent.h"

UCLASS()
class AMyCharacter : public ACharacter {
    GENERATED_BODY()

public:
    AMyCharacter() {
        // 创建相机组件
        CameraComponent = CreateDefaultSubobject<UGameplayCameraComponent>(TEXT("GameplayCamera"));
        CameraComponent->SetupAttachment(RootComponent);
    }

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UGameplayCameraComponent> CameraComponent;
};
```

---

## 创建 Camera Rig

### 1. 创建 Camera Rig 资源

1. Content Browser > Gameplay > Gameplay Camera Rig
2. 打开 Camera Rig 编辑器（基于节点的图）

### 2. 构建 Camera Rig（示例：第三人称）

**节点设置：**
```
Actor Position (Character)
  ↓
Orbit Node (Orbit around character)
  ↓
Offset Node (Shoulder offset)
  ↓
Look At Node (Look at character)
  ↓
Camera Output
```

---

## Camera Nodes

### 位置节点

#### Orbit Node（环绕节点，第三人称）
- 围绕目标 Actor 环绕
- 配置：
  - **Orbit Distance**：与目标的距离（如 300 单位）
  - **Pitch Range**：最小/最大俯仰角
  - **Yaw Range**：最小/最大偏航角

#### Follow Node（跟随节点，平滑跟随）
- 带延迟跟随目标
- 配置：
  - **Lag Speed**：相机追赶的速度
  - **Offset**：与目标的固定偏移

#### Fixed Position Node（固定位置节点）
- 世界空间中的静态相机位置

---

### 旋转节点

#### Look At Node（注视节点）
- 相机指向目标
- 配置：
  - **Target**：要注视的 Actor 或组件
  - **Offset**：注视偏移（如瞄准头部而非脚部）

#### Match Actor Rotation（匹配 Actor 旋转）
- 匹配目标 Actor 的旋转
- 适用于第一人称或载具相机

---

### 修饰器节点

#### Camera Shake（相机震动）
- 添加程序化震动（如脚步声、爆炸）
- 配置：
  - **Shake Pattern**：Perlin 噪声、正弦波、自定义
  - **Amplitude**：震动强度

#### Camera Lag（相机延迟）
- 平滑阻尼相机移动
- 配置：
  - **Lag Speed**：阻尼系数（0 = 瞬时，越高延迟越大）

#### Offset Node（偏移节点）
- 与计算位置的静态偏移
- 适用于肩部相机偏移

---

## Camera Director（在 Rig 之间切换）

### 分配 Camera Rig

```cpp
#include "GameplayCameras/Public/GameplayCameraComponent.h"

void AMyCharacter::SetCameraMode(UGameplayCameraRig* NewRig) {
    if (CameraComponent) {
        CameraComponent->SetCameraRig(NewRig);
    }
}
```

### 在 Camera Rig 之间混合

```cpp
// 0.5 秒内混合到瞄准相机
CameraComponent->BlendToCameraRig(AimingCameraRig, 0.5f);
```

---

## 示例：第三人称 + 瞄准

### 1. 创建两个 Camera Rig

**第三人称 Rig：**
```
Actor Position → Orbit (distance: 300) → Look At → Output
```

**瞄准 Rig：**
```
Actor Position → Orbit (distance: 150) → Offset (shoulder) → Look At → Output
```

### 2. 瞄准时切换

```cpp
UPROPERTY(EditAnywhere, Category = "Camera")
TObjectPtr<UGameplayCameraRig> ThirdPersonRig;

UPROPERTY(EditAnywhere, Category = "Camera")
TObjectPtr<UGameplayCameraRig> AimingRig;

void StartAiming() {
    CameraComponent->BlendToCameraRig(AimingRig, 0.3f); // 0.3 秒混合
}

void StopAiming() {
    CameraComponent->BlendToCameraRig(ThirdPersonRig, 0.3f);
}
```

---

## 常用模式

### 越肩相机

```
Actor Position
  ↓
Orbit Node (distance: 250, yaw offset: 30°)
  ↓
Offset Node (X: 0, Y: 50, Z: 50) // 肩部偏移
  ↓
Look At Node (target: Character head)
  ↓
Output
```

---

### 载具相机

```
Vehicle Position
  ↓
Follow Node (lag: 0.2)
  ↓
Offset Node (behind vehicle: X: -400, Z: 150)
  ↓
Look At Node (target: Vehicle)
  ↓
Output
```

---

### 第一人称相机

```
Character Head Socket
  ↓
Match Actor Rotation
  ↓
Output
```

---

## 相机震动

### 触发相机震动

```cpp
#include "GameplayCameras/Public/GameplayCameraShake.h"

void TriggerExplosionShake() {
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) {
        if (UGameplayCameraComponent* CameraComp = PC->FindComponentByClass<UGameplayCameraComponent>()) {
            CameraComp->PlayCameraShake(ExplosionShakeClass, 1.0f);
        }
    }
}
```

---

## 性能提示

- 限制相机震动频率（不要每帧触发）
- 谨慎使用相机延迟（高延迟值开销大）
- 缓存 Camera Rig 引用（不要每帧搜索）

---

## 调试

### 相机调试可视化

```cpp
// 控制台命令：
// GameplayCameras.Debug 1 - 显示活动 Camera Rig 信息
// showdebug camera - 显示相机调试信息
```

---

## 从旧版相机迁移

### 旧版 Spring Arm + Camera Component

```cpp
// ❌ 旧版：Spring Arm Component
USpringArmComponent* SpringArm;
UCameraComponent* Camera;

// ✅ 新版：Gameplay Camera Component
UGameplayCameraComponent* CameraComponent;
// 在 Camera Rig 资源中构建环绕 + 注视装备
```

---

## 限制（实验性状态）

- **API 不稳定**：预计 UE 5.8+ 会有破坏性变更
- **文档有限**：官方文档仍在完善
- **蓝图支持**：主要面向 C++（蓝图支持在改进中）
- **生产风险**：发布前请充分测试

---

## 来源
- https://docs.unrealengine.com/5.7/en-US/gameplay-cameras-in-unreal-engine/
- UE 5.5+ 发行说明
- **注意：** 此系统为实验性。请始终查看最新官方文档以了解 API 变更。
