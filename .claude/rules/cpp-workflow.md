# C++ 修改工作流规则

> 需要修改 `.h` / `.cpp` 文件时，必须先读取本文件，然后按规则执行。

## 一、C++ 职责

- `Plugins/` 下的 C++：只提供通用 UFUNCTION 接口，不包含项目特定逻辑
- `Source/Sekiro/` 下的 C++：游戏机制实现

## 二、主 Agent 禁止直接修改 C++

主 Agent（主对话线程）**不得直接 Edit/Write `.h` / `.cpp` 文件**。
必须通过 Agent 工具派发给对应的专业 Agent：

- `plugin-programmer` — 插件代码（Plugins/）
- `gameplay-programmer` — 游戏代码（Source/Sekiro/）

即使看起来是小修复（加注释、改参数、修编译错误），也必须通过 Agent。

## 三、Agent 必须编译验证

派发修改 C++ 代码的 Agent 时，prompt 末尾必须追加：

> 修改完成后请自行编译验证，如编译失败则继续修复，直到编译通过后再返回。

## 四、蓝图配置优先

`UPROPERTY(EditAnywhere, BlueprintReadOnly)` 的资产引用
不得在 C++ 构造函数中用 `ConstructorHelpers::FObjectFinder` 硬编码路径。
应在蓝图侧拖拽赋值。
