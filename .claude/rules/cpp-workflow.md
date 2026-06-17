# C++ 修改工作流规则

> **强制规则**：涉及修改 `.h` / `.cpp` 文件时，Claude 必须先完整读取本文件并严格遵守。

## 一、禁止直接修改 C++ 代码

Claude 在任何情况下**不得直接 Edit/Write `.h` / `.cpp` 文件**，无论是 Plugins/ 还是 Source/ 下的。
所有 C++ 代码修改必须通过 Agent 工具派发给对应的专业 Agent：

- `plugin-programmer` — 插件代码（Plugins/）
- `gameplay-programmer` — 游戏代码（Source/Sekiro/）

即使看起来是小修复（加注释、改参数、修编译错误），也必须通过 Agent。绕过此约束属于违规。

## 二、Agent 编译验证要求

派发修改 C++ 代码的 Agent 时，prompt 末尾**必须追加**：

```
修改完成后请自行编译验证，如编译失败则继续修复，直到编译通过后再返回。
```

不允许让 agent 只改代码不编译验证就返回。

## 三、蓝图配置优先于 C++ 硬编码

`UPROPERTY(EditAnywhere, BlueprintReadOnly)` 的资产引用（UInputAction*、UInputMappingContext* 等）
不得在 C++ 构造函数中用 `ConstructorHelpers::FObjectFinder` 硬编码路径。
应在蓝图侧通过 Details 面板拖拽赋值，保持资产引用关系在蓝图层面可见。
