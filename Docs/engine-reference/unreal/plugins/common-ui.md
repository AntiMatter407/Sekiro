# Unreal Engine 5.7 — CommonUI 插件

**最后验证：** 2026-02-13
**状态：** 生产就绪
**插件：** `CommonUI`（内置，在 Plugins 中启用）

---

## 概述

**CommonUI** 是一个跨平台 UI 框架，自动处理手柄、鼠标和触控的输入路由。它旨在让游戏在 PC、主机和移动平台上无缝运行，且只需最少的平台特定代码。

**适用场景：**
- 多平台游戏（主机 + PC）
- 自动手柄/鼠标/触控输入路由
- 输入无关的 UI（同一 UI 适用于任何输入方式）
- 控件焦点和导航
- 操作栏和输入提示

**不适用场景：**
- 仅 PC 且仅鼠标的 UI（标准 UMG 更简单）
- 无导航需求的简单 UI

---

## 与标准 UMG 的主要区别

| 特性 | 标准 UMG | CommonUI |
|---------|--------------|----------|
| **输入处理** | 每个控件手动处理 | 自动路由 |
| **焦点管理** | 基础 | 高级导航 |
| **平台切换** | 手动检测 | 自动 |
| **输入提示** | 硬编码图标 | 根据平台动态显示 |
| **屏幕栈** | 手动管理 | 内置可激活控件 |

---

## 设置

### 1. 启用插件

`Edit > Plugins > CommonUI > Enabled > Restart`

### 2. 配置项目设置

`Project Settings > Plugins > CommonUI`：
- **Default Input Type**：Gamepad（或自动检测）
- **Platform-Specific Settings**：按平台配置输入图标

### 3. 创建 Common Input Settings 资源

1. Content Browser > Input > Common Input Settings
2. 按平台配置输入数据：
   - Default Gamepad Data
   - Default Mouse & Keyboard Data
   - Default Touch Data

---

## 核心控件

### CommonActivatableWidget（屏幕管理）

屏幕/菜单的基类，可被激活/停用。

```cpp
#include "CommonActivatableWidget.h"

UCLASS()
class UMyMenuWidget : public UCommonActivatableWidget {
    GENERATED_BODY()

protected:
    virtual void NativeOnActivated() override {
        Super::NativeOnActivated();
        // 菜单现在可见并获得焦点
        UE_LOG(LogTemp, Warning, TEXT("菜单已激活"));
    }

    virtual void NativeOnDeactivated() override {
        Super::NativeOnDeactivated();
        // 菜单现在隐藏
        UE_LOG(LogTemp, Warning, TEXT("菜单已停用"));
    }

    virtual UWidget* NativeGetDesiredFocusTarget() const override {
        // 返回应接收焦点的控件（如第一个按钮）
        return PlayButton;
    }

private:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UCommonButtonBase> PlayButton;
};
```

---

### CommonButtonBase（输入感知按钮）

替代标准 UMG Button。自动处理手柄/鼠标/键盘输入。

```cpp
#include "CommonButtonBase.h"

UCLASS()
class UMyMenuWidget : public UCommonActivatableWidget {
    GENERATED_BODY()

protected:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UCommonButtonBase> PlayButton;

    virtual void NativeConstruct() override {
        Super::NativeConstruct();

        // 绑定按钮点击（适用于任何输入方式）
        PlayButton->OnClicked().AddUObject(this, &UMyMenuWidget::OnPlayClicked);

        // 设置按钮文本
        PlayButton->SetButtonText(FText::FromString(TEXT("开始游戏")));
    }

    void OnPlayClicked() {
        UE_LOG(LogTemp, Warning, TEXT("点击了开始游戏"));
    }
};
```

---

### CommonTextBlock（带样式的文本）

支持 CommonUI 样式的文本控件。

```cpp
UPROPERTY(meta = (BindWidget))
TObjectPtr<UCommonTextBlock> TitleText;

TitleText->SetText(FText::FromString(TEXT("主菜单")));
```

---

### CommonActionWidget（输入提示）

显示输入提示（如 "按 A 继续"，自动显示正确的按钮图标）。

```cpp
UPROPERTY(meta = (BindWidget))
TObjectPtr<UCommonActionWidget> ConfirmActionWidget;

// 绑定到输入操作
ConfirmActionWidget->SetInputAction(ConfirmInputActionData);
// 自动显示正确图标（Xbox 上显示 A，PlayStation 上显示 X，PC 上显示 Enter）
```

---

## 控件栈（屏幕管理）

### CommonActivatableWidgetStack

管理屏幕栈（如 主菜单 → 设置 → 控制）。

```cpp
#include "Widgets/CommonActivatableWidgetContainer.h"

UPROPERTY(meta = (BindWidget))
TObjectPtr<UCommonActivatableWidgetStack> WidgetStack;

// 将新屏幕压入栈中
void ShowSettingsMenu() {
    WidgetStack->AddWidget(USettingsMenuWidget::StaticClass());
}

// 弹出当前屏幕（返回）
void GoBack() {
    WidgetStack->DeactivateWidget();
}
```

---

## 输入操作（CommonUI 风格）

### 定义输入操作

创建 **Common Input Action Data Table**：
1. Content Browser > Miscellaneous > Data Table
2. 行结构：`CommonInputActionDataBase`
3. 添加操作行（Confirm、Cancel、Navigate 等）

示例行：
- **Action Name**：Confirm
- **Default Input**：Gamepad Face Button Bottom（A/Cross）
- **Alternate Inputs**：Enter（键盘）、鼠标左键

---

### 在控件中绑定输入操作

```cpp
#include "Input/CommonUIActionRouterBase.h"

UCLASS()
class UMyWidget : public UCommonActivatableWidget {
    GENERATED_BODY()

protected:
    virtual void NativeOnActivated() override {
        Super::NativeOnActivated();

        // 绑定输入操作
        FBindUIActionArgs BindArgs(ConfirmInputAction, FSimpleDelegate::CreateUObject(this, &UMyWidget::OnConfirm));
        BindArgs.bDisplayInActionBar = true; // 在操作栏中显示
        RegisterUIActionBinding(BindArgs);
    }

    void OnConfirm() {
        UE_LOG(LogTemp, Warning, TEXT("已确认"));
    }

private:
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    FDataTableRowHandle ConfirmInputAction;
};
```

---

## 焦点与导航

### 自动手柄导航

CommonUI 自动处理手柄导航（D-Pad/摇杆在按钮间移动）。

```cpp
// 在控件蓝图中：
// - 如果控件继承自 CommonButton/CommonUserWidget，则自动可导航
// - 焦点顺序由控件层级和布局决定
```

### 自定义焦点导航

```cpp
// 覆盖焦点导航
virtual UWidget* NativeGetDesiredFocusTarget() const override {
    return FirstButton; // 返回应接收焦点的控件
}
```

---

## 输入模式（游戏 vs UI）

### 切换输入模式

```cpp
#include "CommonUIExtensions.h"

// 切换到仅 UI 模式（暂停游戏，显示光标）
UCommonUIExtensions::PushStreamedGameplayUIInputConfig(this, FrontendInputConfig);

// 返回游戏模式（隐藏光标，恢复游戏玩法）
UCommonUIExtensions::PopInputConfig(this);
```

---

## 平台特定输入图标

### 配置输入图标

1. 为每个平台创建 **Common Input Base Controller Data** 资源：
   - 手柄（Xbox、PlayStation、Switch）
   - 鼠标与键盘
   - 触控

2. 分配平台特定图标：
   - Gamepad Face Button Bottom：`A`（Xbox）、`Cross`（PlayStation）
   - 确认键：`Enter` 图标

3. 分配到 **Common Input Settings** 资源

### 自动显示正确图标

```cpp
// CommonActionWidget 自动为当前平台显示正确图标
UPROPERTY(meta = (BindWidget))
TObjectPtr<UCommonActionWidget> JumpActionWidget;

JumpActionWidget->SetInputAction(JumpInputActionData);
// Xbox 上显示 "A"，PlayStation 上显示 "Cross"，PC 上显示 "Space"
```

---

## 常用模式

### 带导航的主菜单

```cpp
UCLASS()
class UMainMenuWidget : public UCommonActivatableWidget {
    GENERATED_BODY()

protected:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UCommonButtonBase> PlayButton;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UCommonButtonBase> SettingsButton;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UCommonButtonBase> QuitButton;

    virtual void NativeConstruct() override {
        Super::NativeConstruct();

        PlayButton->OnClicked().AddUObject(this, &UMainMenuWidget::OnPlayClicked);
        SettingsButton->OnClicked().AddUObject(this, &UMainMenuWidget::OnSettingsClicked);
        QuitButton->OnClicked().AddUObject(this, &UMainMenuWidget::OnQuitClicked);
    }

    virtual UWidget* NativeGetDesiredFocusTarget() const override {
        return PlayButton; // 菜单打开时焦点在"开始"按钮
    }

    void OnPlayClicked() { /* 开始游戏 */ }
    void OnSettingsClicked() { /* 打开设置 */ }
    void OnQuitClicked() { /* 退出游戏 */ }
};
```

---

### 带返回操作的暂停菜单

```cpp
UCLASS()
class UPauseMenuWidget : public UCommonActivatableWidget {
    GENERATED_BODY()

protected:
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    FDataTableRowHandle BackInputAction; // 在蓝图中分配"Cancel"操作

    virtual void NativeOnActivated() override {
        Super::NativeOnActivated();

        // 绑定"返回"输入（B/Circle/Escape）
        FBindUIActionArgs BindArgs(BackInputAction, FSimpleDelegate::CreateUObject(this, &UPauseMenuWidget::OnBack));
        RegisterUIActionBinding(BindArgs);
    }

    void OnBack() {
        DeactivateWidget(); // 关闭暂停菜单
    }
};
```

---

## 性能提示

- 使用 **CommonActivatableWidgetStack** 进行屏幕管理（自动处理激活/停用）
- 避免每帧创建/销毁控件（复用控件）
- 对复杂菜单使用 **Lazy Widgets**（仅在需要时创建）

---

## 调试

### CommonUI 调试命令

```cpp
// 控制台命令：
// CommonUI.DumpActivatableTree - 显示活动控件层级
// CommonUI.DumpActionBindings - 显示已注册的输入操作
```

---

## 来源
- https://docs.unrealengine.com/5.7/en-US/commonui-plugin-for-advanced-user-interfaces-in-unreal-engine/
- https://docs.unrealengine.com/5.7/en-US/commonui-quickstart-guide-for-unreal-engine/
