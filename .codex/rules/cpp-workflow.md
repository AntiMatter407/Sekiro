# C++ 修改工作流规则

> 需要修改 `.h` / `.cpp` 文件时，必须先读取本文件，然后按规则执行。

## 一、C++ 职责

- `Plugins/` 下的 C++：只提供通用 UFUNCTION 接口，不包含项目特定逻辑
- `Source/Sekiro/` 下的 C++：游戏机制实现

## 二、修改权限分级

根据修改性质决定是否派发 Agent。

### 2.A 必须派发 Agent 的场景

涉及以下任意一条的，必须通过 Agent 工具派发给对应的专业 Agent：

- **方案/模块构建**：新模块、新功能的初次实现（如新 Importer、新 Component）
- **大型重构**：函数拆分、数据流重写、公共工具跨文件提取
- **架构变更**：修改模块依赖、移动代码跨模块

Agent 完成代码编写并**编译通过**后，后续的迭代、修复、优化可交给主 Agent 直接处理。

派发规则：
- `plugin-programmer` — 插件代码（Plugins/）
- `gameplay-programmer` — 游戏代码（Source/Sekiro/）

### 2.B 主 Agent 可直接修改的场景

Agent 完成模块初版并通过编译后，或在以下场景中，主 Agent 可直接 Edit/Write `.h` / `.cpp`：

- 修编译错误（重命名冲突、缺 `#include`、变量未定义等）
- 加诊断日志
- 已有模块的局部改动（路径修正、参数调整、新增函数）
- 已存在的复合分支模式拷贝（如 Commandlet 加一个与已有 `if` 模式相同的分支）
- Commandlet 模式增加

> 即使主 Agent 直接修改，也必须遵循 `.codex/rules/code-style.md` 的代码规范，
> 且修改后必须自行编译验证通过。

## 三、Agent 必须编译验证

派发修改 C++ 代码的 Agent 时，prompt 末尾必须追加：

> 修改完成后请自行编译验证，如编译失败则继续修复，直到编译通过后再返回。

## 四、蓝图配置优先

`UPROPERTY(EditAnywhere, BlueprintReadOnly)` 的资产引用
不得在 C++ 构造函数中用 `ConstructorHelpers::FObjectFinder` 硬编码路径。
应在蓝图侧拖拽赋值。
