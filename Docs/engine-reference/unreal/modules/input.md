# Unreal Engine 5.7 — 输入模块参考

**最后验证：** 2026-02-13
**知识缺口：** UE 5.7 使用 Enhanced Input 作为默认（旧版输入已弃用）

---

## 概述

UE 5.7 输入系统：
- **Enhanced Input**（推荐，UE5 默认）：模块化、可重绑定、基于上下文
- **Legacy Input**（旧版输入）：已弃用，新项目避免使用

---

## Enhanced Input 系统

### 设置 Enhanced Input

1. **启用插件**：`Edit > Plugins > Enhanced Input`（UE5 中默认启用）
2. **项目设置**：`Engine > Input > Default Classes > Default Player Input Class = EnhancedPlayerInput`

---

### 创建 Input Actions

1. Content Browser > Input > Input Action
2. 命名（如 `IA_Jump`、`IA_Move`）
3. 配置：
   - **Value Type**：Digital（bool）、Axis1D（float）、Axis2D（Vector2D）、Axis3D（Vector）

Input Action 示例：
- `IA_Jump`：Digital（bool）
- `IA_Move`：Axis2D（Vector2D）
- `IA_Look`：Axis2D（Vector2D）
- `IA_Fire`：Digital（bool）

---

### 创建 Input Mapping Context

1. Content Browser > Input > Input Mapping Context
2. 命名（如 `IMC_Default`）
3. 添加映射：
   - `IA_Jump` → 空格键
   - `IA_Move` → W/A/S/D 键（组合 X/Y）
   - `IA_Look` → 鼠标 XY
   - `IA_Fire` → 鼠标左键

---

### 在 C++ 中绑定输入

```cpp
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"

class AMyCharacter : public ACharacter {
public:
    // Input Actions（在蓝图中分配）
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> LookAction;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputAction> JumpAction;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

protected:
    virtual void BeginPlay() override {
        Super::BeginPlay();

        // 添加 Input Mapping Context
        if (APlayerController* PC = Cast<APlayerController>(Controller)) {
            if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
                ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())) {
                Subsystem->AddMappingContext(DefaultMappingContext, 0);
            }
        }
    }

    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override {
        Super::SetupPlayerInputComponent(PlayerInputComponent);

        UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
        if (EIC) {
            // 绑定操作
            EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
            EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

            EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMyCharacter::Move);
            EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMyCharacter::Look);
        }
    }

    void Move(const FInputActionValue& Value) {
        FVector2D MoveVector = Value.Get<FVector2D>();

        if (Controller) {
            AddMovementInput(GetActorForwardVector(), MoveVector.Y);
            AddMovementInput(GetActorRightVector(), MoveVector.X);
        }
    }

    void Look(const FInputActionValue& Value) {
        FVector2D LookVector = Value.Get<FVector2D>();

        if (Controller) {
            AddControllerYawInput(LookVector.X);
            AddControllerPitchInput(LookVector.Y);
        }
    }
};
```

---

## 输入触发器 (Input Triggers)

### 触发类型

Input Action 可以设置触发器来控制何时触发：
- **Pressed**：输入开始时
- **Released**：输入结束时
- **Hold**：按住一段时间
- **Tap**：快速按下
- **Pulse**：按住时重复触发

### 在编辑器中添加触发器

1. 打开 Input Action 资源
2. Triggers > Add > 选择触发类型（如 `Hold`）
3. 配置（如 Hold Time = 0.5s）

---

## 输入修饰器 (Input Modifiers)

### 修饰器类型

修饰器用于转换输入值：
- **Negate**：翻转符号（-1 ↔ 1）
- **Dead Zone**：忽略小幅度输入
- **Scalar**：乘以值
- **Smooth**：随时间平滑

### 在编辑器中添加修饰器

1. 打开 Input Action 资源
2. Modifiers > Add > 选择修饰器（如 `Negate`）
3. 配置

---

## Input Mapping Contexts（上下文切换）

### 多上下文

```cpp
// 定义上下文
UPROPERTY(EditAnywhere, Category = "Input")
TObjectPtr<UInputMappingContext> DefaultContext;

UPROPERTY(EditAnywhere, Category = "Input")
TObjectPtr<UInputMappingContext> VehicleContext;

// 切换上下文
void EnterVehicle() {
    if (APlayerController* PC = Cast<APlayerController>(Controller)) {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())) {
            Subsystem->RemoveMappingContext(DefaultContext);
            Subsystem->AddMappingContext(VehicleContext, 0);
        }
    }
}
```

---

## 旧版输入（已弃用）

### 旧版输入绑定

```cpp
// ❌ 已弃用：新项目不要使用

void AMyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) {
    // 旧版操作绑定
    PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);

    // 旧版轴绑定
    PlayerInputComponent->BindAxis("MoveForward", this, &AMyCharacter::MoveForward);
}

void MoveForward(float Value) {
    AddMovementInput(GetActorForwardVector(), Value);
}
```

**迁移：** 请改用 Enhanced Input。

---

## 手柄输入

### Enhanced Input 的手柄支持

```cpp
// Input Mapping Context：
// - IA_Move → 手柄左摇杆
// - IA_Look → 手柄右摇杆
// - IA_Jump → 手柄底部按钮（A/Cross）

// 无需代码修改，只需在 Input Mapping Context 中添加手柄映射
```

---

## 触控输入（移动端）

### Enhanced Input 的触控支持

```cpp
// Input Mapping Context：
// - IA_Move → 触控（虚拟摇杆）
// - IA_Look → 触控（滑动）

// 使用 Touch Interface 资源进行虚拟控制
```

---

## 运行时重新绑定输入

### 更改按键映射

```cpp
#include "PlayerMappableInputConfig.h"

// 获取子系统
UEnhancedInputLocalPlayerSubsystem* Subsystem = /* 获取子系统 */;

// 获取玩家可映射按键
FPlayerMappableKeySlot KeySlot = FPlayerMappableKeySlot(/*..*/);
FKey NewKey = EKeys::F; // 重新绑定到 F 键

// 应用新映射
Subsystem->AddPlayerMappedKey(/*..*/);
```

---

## 输入调试

### 调试输入

```cpp
// 控制台命令：
// showdebug input - 显示输入调试信息

// 日志输出输入值：
UE_LOG(LogTemp, Warning, TEXT("Move Input: %s"), *MoveVector.ToString());
```

---

## 常用模式

### 检查按键是否按下（快速方式）

```cpp
// 仅用于调试（不推荐用于实际游戏）
if (GetWorld()->GetFirstPlayerController()->IsInputKeyDown(EKeys::SpaceBar)) {
    // 空格键被按下
}
```

---

## 来源
- https://docs.unrealengine.com/5.7/en-US/enhanced-input-in-unreal-engine/
- https://docs.unrealengine.com/5.7/en-US/enhanced-input-action-and-input-mapping-context-in-unreal-engine/
