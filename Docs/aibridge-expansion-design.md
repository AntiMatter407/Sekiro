# AIBridge 插件扩展技术设计文档

**版本**: 0.1.0
**日期**: 2026-06-11
**状态**: 待审批

---

## 1. 概述 (Overview)

### 1.1 目标

将 AIBridge JSON-RPC 服务从当前的 6 个工具扩展到 **8 个工具**，新增：

- **EnhancedInput 工具** — 创建/配置 UInputAction、UInputMappingContext 资产
- **AnimBlueprint 工具** — 创建动画蓝图、操作状态机、管理动画节点
- **扩展 Blueprint 工具** — 修复 bridge.py 不一致（`add_node`），扩展 `set_property` 类型支持

### 1.2 动机

当前 AIBridge 的 Blueprint 操作仅限于 Actor Blueprint 和基础变量/函数/组件操作，无法处理：

1. **Enhanced Input 资产管线** — 需要手动在编辑器创建 InputAction/IMC，无法通过 AI 批量创建
2. **动画蓝图管线** — 状态机、状态、转换、动画节点的创建是动画系统的核心操作
3. **复杂属性设置** — FVector/FRotator/FTransform 等结构体属性无法设置
4. **蓝图节点操作** — bridge.py 已定义 `add_node` 但 C++ 未实现

---

## 2. 架构设计

### 2.1 现有架构回顾

```
Client (bridge.py) 
  → TCP (127.0.0.1:9877) 
  → FSKAIBridgeServer (独立线程) 
  → AsyncTask → GameThread 
  → USKAIBridgeSubsystem::OnMessageReceived()
  → ToolRegistry → ISKAIToolInterface::Execute()
```

### 2.2 新增组件

```
                        USKAIToolRegistry
                        ├── USKEditorStateTool      (已有: editor.query)
                        ├── USKConsoleTool           (已有: console.execute)
                        ├── USKAssetTool             (已有: asset)
                        ├── USKPythonTool            (已有: python.execute)
                        ├── USKCompileTool           (已有: compile.run)
                        ├── USKBlueprintTool         (扩展: blueprint)
                        ├── USKEnhancedInputTool     (新增: enhanced_input)
                        └── USKAnimBlueprintTool     (新增: anim_blueprint)
```

### 2.3 模块依赖变更

```cpp
// SekiroAIBridge.Build.cs — PrivateDependencyModuleNames 新增
"EnhancedInput",        // UInputAction, UInputMappingContext, UInputTrigger/Modifier
"AnimGraph",            // UAnimGraphNode_*, UAnimStateNode, UAnimationStateMachineGraph
"AnimGraphRuntime",     // FAnimNode_SequencePlayer, FAnimNode_BlendSpacePlayer
```

---

## 3. 工具详细设计

### 3.1 USKEnhancedInputTool

**工具名**: `enhanced_input`
**确认策略**: 仅 `map_key` / `unmap_key` 需确认

#### 3.1.1 Actions

| Action | 参数 | 返回值 | 描述 |
|--------|------|--------|------|
| `create_input_action` | path, value_type?, triggers[]?, modifiers[]?, b_consume_input?, b_trigger_when_paused? | {path, class, success} | 创建 UInputAction |
| `create_mapping_context` | path | {path, class, success} | 创建 UInputMappingContext |
| `map_key` | context_path, action_path, key, triggers[]?, modifiers[]? | {context, action, key, success} | 绑定按键映射 |
| `unmap_key` | context_path, action_path, key | {context, success} | 移除按键映射 |
| `get_info` | path | {name, path, class, value_type, mappings[], ...} | 查询详情 |

#### 3.1.2 输入触发器/修改器映射表

触发器（Triggers）:
| JSON type | C++ 类 | 额外参数 |
|-----------|--------|----------|
| `"Pressed"` | UInputTriggerPressed | — |
| `"Down"` | UInputTriggerDown | — |
| `"Released"` | UInputTriggerReleased | — |
| `"Hold"` | UInputTriggerHold | hold_time_threshold (float, 默认 0.5) |
| `"Tap"` | UInputTriggerTap | tap_release_time_threshold (float, 默认 0.2) |
| `"Pulse"` | UInputTriggerPulse | interval (float, 默认 1.0) |
| `"ChordedAction"` | UInputTriggerChordAction | chord_action_path (string) |

修改器（Modifiers）:
| JSON type | C++ 类 | 额外参数 |
|-----------|--------|----------|
| `"DeadZone"` | UInputModifierDeadZone | lower_threshold, upper_threshold, type (axial/radial) |
| `"Scalar"` | UInputModifierScalar | scalar (FVector, 默认 1,1,1) |
| `"Negate"` | UInputModifierNegate | x, y, z (bool, 默认 true,true,true) |
| `"SwizzleAxis"` | UInputModifierSwizzleAxis | order (XYZ/XZY/YXZ 等) |
| `"Smooth"` | UInputModifierSmooth | — |

#### 3.1.3 关键实现

```cpp
// create_input_action 核心流程
IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
UInputAction* IA = Cast<UInputAction>(AssetTools.CreateAsset(Name, Path, UInputAction::StaticClass(), nullptr));
IA->ValueType = ParseValueType(Args);  // "bool"→EInputActionValueType::Boolean, etc.

// 添加触发器
for (auto& Trig : TriggersArray) {
    UInputTrigger* TriggerObj = NewObject<UInputTrigger_Pressed>(IA, TriggerClass);
    IA->Triggers.Add(TriggerObj);
}
// 类似添加 Modifiers
IA->MarkPackageDirty();
UEditorAssetLibrary::SaveAsset(AssetPath, false);
```

---

### 3.2 USKAnimBlueprintTool

**工具名**: `anim_blueprint`
**确认策略**: 所有操作均需确认（修改图结构属高风险）

#### 3.2.1 Actions

| Action | 关键参数 | 描述 |
|--------|----------|------|
| `create` | path, skeleton_path | 创建 UAnimBlueprint 并关联骨架 |
| `add_state` | path, state_name, x?, y? | 向状态机添加状态节点 |
| `add_transition` | path, from_state, to_state, crossfade_duration?, blend_mode?, bidirectional? | 创建状态间转换 |
| `add_node` | path, state_name, node_type, asset_path?, play_rate?, loop? | 向状态的 AnimGraph 添加动画节点 |
| `get_info` | path | 输出完整状态机结构（状态/转换/节点） |
| `compile` | path | 编译 AnimBlueprint |

#### 3.2.2 动画蓝图图结构

AnimBlueprint 的内部图层次：

```
UAnimBlueprint
├── EventGraph (UEdGraph)
├── AnimGraph (UEdGraph)
│   └── UAnimGraphNode_StateMachine (状态机入口节点)
│       └── EditorStateMachineGraph (UAnimationStateMachineGraph)
│           ├── UAnimStateEntryNode (入口)
│           ├── UAnimStateNode "Idle"
│           │   └── BoundGraph (UAnimationStateGraph)
│           │       ├── UAnimGraphNode_SequencePlayer
│           │       └── UAnimGraphNode_StateResult
│           ├── UAnimStateNode "Run"
│           │   └── BoundGraph (UAnimationStateGraph)
│           │       └── UAnimGraphNode_BlendSpacePlayer
│           └── UAnimStateTransitionNode (Idle→Run)
│               └── BoundGraph (UAnimationTransitionGraph)
```

#### 3.2.3 关键实现流程

**add_state**:
```
1. LoadObject<UAnimBlueprint>(AssetPath)
2. 查找 AnimGraph → 查找 UAnimGraphNode_StateMachine
   - 若不存在：FGraphNodeCreator 创建状态机节点 → 连接到 AnimGraph 的 Result 节点
3. 获取 EditorStateMachineGraph
   - 若为 null：NewObject<UAnimationStateMachineGraph> → 设置 Schema
4. FGraphNodeCreator<UAnimStateNode> 创建状态节点
5. StateNode->Rename(Name) + 设置位置
6. Finalize() — 自动创建 BoundGraph
7. 可选：连接到 Entry 节点
8. MarkPackageDirty + SaveAsset
```

**add_transition**:
```
1. 在 EditorStateMachineGraph 中通过标题查找 from/to 状态节点
2. FGraphNodeCreator<UAnimStateTransitionNode> 创建转换节点
3. TransitionNode->CreateConnections(FromState, ToState)
4. 设置属性：CrossfadeDuration, BlendMode, bBidirectional
5. Finalize() — 自动创建 BoundGraph (过渡规则图)
6. MarkPackageDirty + SaveAsset
```

**add_node** (添加动画节点到状态):
```
1. 在状态机图中查找目标 UAnimStateNode
2. 获取其 BoundGraph (UAnimationStateGraph)
3. 根据 node_type 创建对应节点:
   - "sequence_player": FGraphNodeCreator<UAnimGraphNode_SequencePlayer>
     → SeqNode->Node.SetSequence(LoadObject<UAnimSequence>(AssetPath))
   - "blend_space_player": FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer>
     → BspNode->Node.SetBlendSpace(LoadObject<UBlendSpace>(AssetPath))
4. 设置位置 + Finalize()
5. 连接节点:
   - 找到 StateResult 节点 (UAnimGraphNode_StateResult)
   - TryCreateConnection(新节点->FindPin("Pose", EGPD_Output), 
                         StateResult->FindPin("Pose", EGPD_Input))
6. MarkPackageDirty + CompileBlueprint
```

#### 3.2.4 node_type 支持的完整列表

| node_type | C++ 类 | 必需额外参数 |
|-----------|--------|-------------|
| `sequence_player` | UAnimGraphNode_SequencePlayer | asset_path (UAnimSequence) |
| `blend_space_player` | UAnimGraphNode_BlendSpacePlayer | asset_path (UBlendSpace) |
| `state_machine` | UAnimGraphNode_StateMachine | state_machine_name |

---

### 3.3 USKBlueprintTool 扩展

#### 3.3.1 新增 action: `add_node`

```
参数: path, graph_name ("EventGraph" 默认), node_type, x?, y?, node_params?
```

支持的 node_type（K2 节点）:
- `"PrintString"` — param: InString
- `"CallFunction"` — param: function_name (需指定目标函数路径)
- `"Branch"` — if/else 控制流
- `"Sequence"` — 顺序执行
- `"ForLoop"` — 整数循环

实现方式：
```cpp
// 找到目标图
UEdGraph* TargetGraph = nullptr;
for (UEdGraph* Graph : BP->FunctionGraphs) {
    if (Graph->GetName() == GraphName) { TargetGraph = Graph; break; }
}
if (!TargetGraph) TargetGraph = BP->UbergraphPages.Num() > 0 ? BP->UbergraphPages[0] : nullptr;

// 通过 Schema 创建节点
const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
// 使用 FGraphNodeCreator 或 Schema->SpawnNodeFromTemplate
```

#### 3.3.2 扩展 `set_property` 类型

在现有的 `FBoolProperty` / `FFloatProperty` / `FDoubleProperty` / `FIntProperty` / `FStrProperty` 之后，增加：

| FProperty 类型 | 设置方式 |
|----------------|---------|
| FStructProperty (FVector) | `TBaseStructure<FVector>::Get()` 匹配 → `ImportText_Direct` |
| FStructProperty (FRotator) | 同上，格式 `"P=0,Y=90,R=0"` |
| FStructProperty (FTransform) | 同上，格式 `"(X=1,Y=2,Z=3),(P=0,Y=0,R=0),(X=1,Y=1,Z=1)"` |
| FStructProperty (FColor) | `ImportText_Direct("(R=255,G=0,B=0,A=255)")` |
| FStructProperty (FLinearColor) | `ImportText_Direct("(R=1.0,G=0.5,B=0.0,A=1.0)")` |
| FObjectProperty | `LoadObject` → `SetObjectPropertyValue` |
| FNameProperty | `SetPropertyValue(FName(*Value))` |
| FTextProperty | `SetPropertyValue(FText::FromString(Value))` |
| FByteProperty (枚举) | `SetPropertyValue(EnumValue)` 或 `ImportText_Direct` |
| FEnumProperty | `SetEnumPropertyValue(EnumValue)` |

---

## 4. 文件清单

### 4.1 新建文件

```
Plugins/SekiroAIBridge/Source/SekiroAIBridge/
├── Public/Tools/
│   ├── USKEnhancedInputTool.h       (~35行)
│   └── USKAnimBlueprintTool.h       (~55行)
└── Private/Tools/
    ├── USKEnhancedInputTool.cpp     (~350行)
    └── USKAnimBlueprintTool.cpp     (~500行)
```

### 4.2 修改文件

```
SekiroAIBridge.Build.cs              (+3模块依赖)
USKAIBridgeSubsystem.cpp              (+4行: include + register)
USKBlueprintTool.h                    (+2行: HandleAddNode声明)
USKBlueprintTool.cpp                  (~150行: add_node + set_property扩展)
.claude/skills/aibridge/bridge.py     (~100行: 新命令封装)
.claude/skills/aibridge/SKILL.md      (~30行: 文档更新)
```

---

## 5. 风险与限制

### 5.1 已知风险

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 新 AnimBlueprint 无默认状态机图 | add_state 失败 | 首次操作时自动创建状态机节点和图 |
| FGraphNodeCreator 未 Finalize | 编辑器崩溃 | RAII 包裹，严格作用域控制 |
| EnhancedInput 模块在 Target 中不可用 | 编译失败 | 仅 Editor 构建（插件已是 Editor 类型） |
| 动画节点引脚名不一致 | TryCreateConnection 失败 | 先通过 FindPin 查找，失败则返回明确错误 |
| 蓝图已编译但脏状态 | CDO 属性读取到旧值 | set_property 前强制编译或至少检查状态 |

### 5.2 当前限制

1. **动画节点类型有限**: 首期仅支持 SequencePlayer 和 BlendSpacePlayer，更多节点类型后续扩展
2. **状态机嵌套**: 仅支持一层嵌套（AnimGraph → StateMachine），深层嵌套后续支持
3. **K2 add_node**: 仅支持常见节点类型，不支持自定义 Blueprint 节点
4. **set_property**: 不支持 TArray/TMap 容器类型（UE 反射系统复杂）

---

## 6. 验收标准

- [ ] Build.cs 修改后项目编译零错误
- [ ] UE5 编辑器启动后日志显示 8 个工具已注册
- [ ] `enhanced_input create_input_action` 能创建可用的 UInputAction
- [ ] `enhanced_input create_mapping_context` + `map_key` 能绑定按键
- [ ] `anim_blueprint create` 能创建关联骨架的 AnimBlueprint
- [ ] `anim_blueprint add_state` 能在状态机中创建状态
- [ ] `anim_blueprint add_transition` 能连接两个状态
- [ ] `anim_blueprint add_node` 能在状态中添加 SequencePlayer/BlendSpacePlayer
- [ ] `blueprint set_property` 能正确设置 FVector/FRotator/FTransform
- [ ] `blueprint add_node` 能向函数图添加 K2 节点
- [ ] bridge.py 所有新命令能通过 JSON-RPC 正常往返
- [ ] 只读模式下高风险操作被正确拒绝
