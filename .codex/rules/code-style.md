---
paths:
  - "Source/Sekiro/**/*.h"
  - "Source/Sekiro/**/*.cpp"
  - "Plugins/**/*.h"
  - "Plugins/**/*.cpp"
  - "Content/Script/**/*.lua"
---

# 代码风格规则

## Lua 脚本规则

`Content/Script/**/*.lua` 的完整规范见 `Docs/lua-code-style.md`。以后新增或修改项目 Lua 时，必须按该文档检查模块结构、命名、详细中文注释、UnLua 类方法绑定、C++ 桥接边界和动画状态机约定。

`Plugins/UnLua*`、`Plugins/UnLuaExtensions*` 下的插件/第三方 Lua 不套用项目业务 Lua 风格，除非明确是在项目侧扩展。

## 0. 命名前缀（最高准则）

### 项目代码 → SK 缩写

`Source/Sekiro/` 下的所有类型（class/struct/enum）和文件名使用 `SK` 缩写，紧跟在 UE 前缀（A/U/F/E/I/T）之后。

```cpp
// 正确 — SK 缩写
class ASKCharacter;
class USKAnimInstance;
class USKWeaponComponent;
struct FSKDamageInfo;
enum class ESKStanceType;

// 错误
class ASekiroCharacter;     // 项目代码不用全称
class ACharacter;           // 缺少 SK 前缀
```

### 插件代码 → Sekiro 全称

`Plugins/` 下的 Sekiro 项目插件（SekiroImport、SekiroAIBridge 等）使用完整 `Sekiro` 前缀。

```cpp
// 正确 — 插件用全称
class USekiroImportLibrary;
class ASekiroAIBridgeActor;
struct FSekiroImportSettings;

// 错误
class USKImportLibrary;     // 插件不用 SK 缩写
```

**例外**：模块名（`Sekiro`）和 DLL 导出宏（`SEKIRO_API`）保持原样不变。

## 1. UPROPERTY 格式

UPROPERTY 宏必须独占一行，变量声明在下一行，同行加中文注释说明用途。

```cpp
// 正确
UPROPERTY(EditDefaultsOnly, Category = "Combat")
float AttackCooldown = 0.5f;             // 攻击冷却

// 错误
UPROPERTY(EditDefaultsOnly, Category = "Combat") float AttackCooldown = 0.5f;
```

## 2. 变量中文注释

所有 UPROPERTY 变量和非 UPROPERTY 成员变量均需同行中文注释。局部变量不需要。

## 3. 命名禁止单字母下划线前缀

禁止 `S_`、`R_`、`K_`、`D_` 等单字母+下划线前缀。使用完整单词：

```cpp
// 正确
float BaseStrength = 100.f;              // 基础强度
float WaveGrowthRate = 0.15f;            // 每波强度增长率

// 错误
float S_Base = 100.f;
float R_Wave = 0.15f;
```

## 4. 嵌套控制流括号

嵌套（相同或不同关键字嵌套）的单行 `if`/`for`/`while`，仅最内层可省略括号且不换行，外层必须保留括号和换行。

非嵌套单语句可直接省略括号。

```cpp
// 正确：if 嵌套 if，外层有括号，最内层省略
if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
{
    if (DefaultMapping) Sub->AddMappingContext(DefaultMapping, 0);
}

// 正确：if 嵌套 for，外层有括号，最内层省略
if (Json->TryGetArrayField(TEXT("Items"), Arr))
{
    for (const auto& Val : *Arr)
        OutNames.Add(FName(*Val->AsString()));
}

// 正确：非嵌套单语句，直接省略
if (!GS) return;

// 错误：外层也省略括号
if (Data.IsValid())
    for (const auto& Val : Data.Items)
        Process(Val);
```

## 5. 嵌套深度阈值

大括号嵌套 ≥4 层才考虑优化（拆子函数、卫语句等），2-3 层嵌套无需强行拆平。

## 6. 指针集中判空

优先将同作用域内的指针合并到一个 `if` 中判空。函数中间才获取的指针，就地判空即可，不强求集中到顶部。

```cpp
// 推荐（同作用域合并判空）
void UMyClass::DoWork(AActor* Actor, UComponent* Comp)
{
    if (!Actor || !Comp) return;

    // 主逻辑
}

// 中间获取的指针，就地判空
void UMyClass::DoWork()
{
    AActor* Owner = GetOwner();
    if (!Owner) return;

    // ... 中间逻辑 ...

    UComponent* Comp = Owner->FindComponentByClass<UComp>();
    if (!Comp) return;                   // 中间获取，就地判空

    Comp->DoSomething();
}

// 错误（能合在一起的分开写）
void UMyClass::DoWork(AActor* Actor, UComponent* Comp)
{
    if (!Actor) return;
    if (!Comp) return;

    // ...
}
```

## 7. 禁止 auto

禁止使用 `auto` 关键字声明变量，lambda 捕获除外。

```cpp
// 正确
TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);
TWeakObjectPtr<ASKEnemy> Weak = ActiveEnemies[i];

// 正确（lambda 允许）
auto SetSlot = [&](UTextBlock* TB, int32 Idx) { ... };

// 错误
auto Json = MakeShareable(new FJsonObject);
auto Weak = ActiveEnemies[i];
```

## 8. 访问修饰符顺序

类内声明顺序固定为 `public:` → `protected:` → `private:`，不允许多次穿插。

## 9. 功能块分组

每个访问修饰符区块内按功能用 `// ── XXX ──` 分隔注释分组，相关函数/变量放在同一组。

```cpp
public:
    // ── 战斗接口 ──────────────────────────────────────────────
    void TakeAttackDamage(float Damage, AActor* Instigator);
    float GetCurrentHP() const { return CurrentHP; }

    // ── 背包访问 ──────────────────────────────────────────────
    USKInventoryComponent* GetInventory() const { return Inventory; }
```

## 10. .cpp 实现顺序

`.cpp` 文件中函数实现顺序必须严格匹配 `.h` 中的声明顺序：先 public，再 protected，再 private，同区块内按声明顺序排列。
