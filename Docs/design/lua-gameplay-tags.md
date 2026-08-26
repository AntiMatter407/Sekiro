# SekiroGameplay 插件 — GameplayTag 生成技术方案

| 进度文档 | 状态 |
|----------|------|
| [独立子需求](../plan/lua-gameplay-tags.md) | C++ 插件已实现并编译；编辑器加载与首份资产迁移待确认 |

## 1. 用户操作与产物

1. 启用 **SekiroGameplay** 插件，在 UE 主菜单栏打开 **Lua玩法**（英文 **LuaGameplay**）顶层菜单。
2. 选择 **GameplayTag 的 Lua 导入**（英文 **Import GameplayTags from Lua**），直接打开该功能窗口，指定一个 Lua 文件及输出资产长包名。
3. 先预览展开后的标签与中文说明；非法 Lua 数据、重复标签或路径问题会给出诊断。
4. 点击生成，创建或更新原生 **UDataTable**，行结构为 **FGameplayTagTableRow**。
5. 默认将数据表注册到项目 **GameplayTagTableList** 并刷新编辑器标签树；生成后可以定位资产。

项目示例输入为 `Content/Script/Gameplay/Sekiro/AbilitySystem/GameplayTags.lua`。
建议输出为 `/Game/Gameplay/AbilitySystem/DT_GameplayTags`，这是项目使用示例，不是插件内硬编码的路径。
当前输入预计生成 12 行：7 个现有 Survival 业务标签及 5 个父级标签；首份 DataTable 尚未生成。

**GameplayTag 本身不是独立的 UAsset 类。** 此处的“标签资产”是 UE 原生标签 DataTable，
以及项目 GameplayTagTableList 对该表的注册引用。无需给每个 Tag 创建自定义资产。

## 2. 插件定位和扩展边界

插件目录为 `Plugins/SekiroGameplay/`，首期只有 `SekiroGameplayEditor` 编辑器模块。
当前功能不需要运行时模块，因此不创建空模块；以后新增运行时能力时可再拆出独立模块。

插件是通用 Gameplay 编辑器工具入口，GameplayTag 只是第一个功能，不能把整个插件命名或限制成 Tag 导入器。
主菜单栏入口与文件/编辑/工具等同级；内部菜单名为 LuaGameplay，中文显示“Lua玩法”，英文显示“LuaGameplay”。
菜单位于帮助之前，不再在“工具”下面挂单个窗口入口。中英文本通过 UE FPolyglotTextData 注册为编辑器本地化文本，随语言切换，无需修改当前编辑器语言。

- 公共模块接口提供 RegisterTool / UnregisterTool，以工具 ID、本地化显示名称及 Widget 工厂注册功能。
- 下拉菜单在展开时读取注册表，每个功能一个菜单项；注销功能后下次展开不再出现。
- OpenTool(ToolId) 直接打开对应工具页；同一页已打开时只聚焦，切换功能才重建页面。
- 工具窗口保留页间导航；每个功能独立组织 UI 和业务库，主入口为顶层下拉菜单。
- 首个 GameplayTag 页面调用公开 C++ UFUNCTION；不在 Slate 按钮里实现另一套生成算法。
- 后续 AttributeSet 配置、GameplayEffect、Ability 等工具可按同一机制扩展；本轮不预先实现这些业务。
- 插件只依赖引擎模块，不引用 Source/Sekiro、角色类、项目 Lua 模块路径或本机绝对路径。
- 不依赖 Python、UnLua 或外部 Lua 运行时；使用 C++ 解析受限的声明式 Lua 数据。

生成后的 DataTable 使用引擎行结构，不依赖编辑器插件类，运行时只由 GAS 加载标签字典。

## 3. 数据流与职责

```text
用户指定 Lua 文件
  → C++ Lua 数据解析器：词法、语法、名称与边界校验
  → 稳定排序的标签条目：完整名称 + DevComment
  ├─ 编辑器窗口只读预览
  └─ C++ 资产生成库
       → UDataTable<FGameplayTagTableRow> + 来源/所有权元数据
       → 保存 .uasset
       → GameplayTagTableList 注册
       → 编辑器刷新 GameplayTagsManager
```

C++ 提供通用校验和资产接口；Lua 表定义项目的标签层次和语义。
窗口负责文件选择、参数输入、预览与结果反馈，不负责角色状态编排。

## 4. Lua 表契约

```lua
return {
    State = {
        _Comment = "角色状态",
        Life = {
            Dead = "死亡流程已经收尾",
        },
    },
}
```

生成 State、State.Life、State.Life.Dead 三行，标签层级通过点连接。

- 分支是嵌套普通表，每一级都生成 Tag；空表表示没有说明的叶节点。
- 字符串叶值写入 DevComment；true 表示没有说明的标签。
- _Comment 是保留的说明字段，不生成标签，必须为字符串；根容器说明不输出，分支说明不继承。
- 节点名只能使用 ASCII 字母、数字、下划线，首字符为字母或下划线；不允许直接在键里写点。
- 接受标识符或带引号的方括号键、单/双引号短字符串、逗号/分号分隔及 Lua 行注释/长注释。
- 短字符串支持换行、回车、制表、引号及反斜杠转义，不接受其他转义或原始多行字符串。
- 入口为 return 表，或单个 local 表声明后 return 同一变量；不执行 Lua。
- 拒绝 require、函数调用、循环、表达式、元表、数组、数字、false、nil 和多返回值。
- 重复键、FName 大小写冲突、None 根名、非法字符均报错；语法诊断带行列。
- 上限为 1 MiB 源文件、64 级层次、10000 个标签，完整标签名不超过 1023 个 ASCII 字符。

这些规则保留上一版表结构，现有项目 Lua 无需重写数值或标签语义。

## 5. C++ 公共生成接口

`USekiroGameplayTagLibrary` 是编辑器 UBlueprintFunctionLibrary：

| 接口 | 输入 | 输出与副作用 |
|------|------|--------------|
| PreviewGameplayTags | LuaFilePath | bool、标签数组、错误文本；不修改资产或配置 |
| GenerateGameplayTagTable | LuaFilePath、AssetPackagePath、bRegisterTagSource | bool、结构化结果；生成并保存 DataTable，可选注册标签源 |

生成结果明确区分 AssetObjectPath、TagCount、bAssetSaved、bSourceRegistered、Message。
资产保存与项目配置保存不是跨文件事务；如果注册失败但表已保存，返回失败及部分成功信息，不能显示“全部完成”。

这些 API 可由编辑器蓝图或自动化调用。调试脚本可以调用 C++，但不是实现该功能所需的生产依赖。

## 6. 资产与配置写入保护

- 仅在游戏线程、非 PIE 状态执行资产生成；预览与生成前均校验输入。
- 输出为用户指定的 /Game 长包名，不把磁盘路径当作资产包名，不允许写到引擎资产。
- 新表以完整 Tag 名作为行名，行结构固定为 FGameplayTagTableRow。
- 已有目标必须是本生成器拥有、来源匹配且行结构正确的表；拒绝覆盖用户手写表、其他生成器资产或其他 UObject。
- 来源与所有权元数据保存在资产包；更新只替换当前生成表的行，不删除其他资产。
- 工程内 Lua 来源记录为相对工程目录的路径，使仓库搬迁后仍可匹配；工程外文件保留绝对路径。
- 检测只读文件和保存失败，失败不得把未保存变更报告为成功；已有表保存失败需恢复内存数据。
- 注册使用 GameplayTagTableList，保留其他来源，不修改 ImportTagsFromConfig 或其他 GameplayTags 设置。
- UE5.2 的 UpdateSinglePropertyInConfigFile 不支持数组，不能用它保存 GameplayTagTableList；使用保留其他配置内容的专门写入过程。
- 生成后不再额外输出一份相同标签的 INI，避免同一批标签长期保留两个维护来源。
- 修改/删除已有 Tag 不自动重写蓝图、GE 或存档引用，不猜测重定向规则。

## 7. 项目接入与旧工具迁移

本需求替代上一版 Python 方案，生产入口改为编辑器插件。
原 Script/generate_gameplay_tags.py 和对应 Python 测试移除，契约覆盖迁入插件 C++ 测试代码。

Survival 保持现有七个 Tag 名称及“全部必需标签存在才绑定资源策略”的规则，无需重新改变角色接口。
Lua 仍是唯一手写字典，DataTable 是生成资产。标签定义不会自动向 Actor 授予 Dead 或 Immune 等状态。

迁移顺序必须保持字典可用：

1. 插件编译并由编辑器加载。
2. 用现有 Lua 生成并注册 DT_GameplayTags，确认生成结果、行结构和注册引用。
3. 再移除上一版工具拥有的 Config/Tags/SKGameplayTags.ini，不触碰其他标签源。
4. 更新窗口使用说明与验证记录。

若当前编辑器需要重启，先由用户保存未保存内容并授权重启。未完成生成时保留旧 INI，不把缺失资产路径写入注册表。

## 8. 验证范围

默认执行插件与项目 UBT 编译、C++/Lua 静态检查，以及得到编辑器操作授权后的必要资产生成。
C++ 测试覆盖 Lua 解析、冲突、越界和资产输入边界，编译但不执行；不启动 PIE、不模拟输入、不运行战斗场景。

本轮插件 UBT 编译通过，主任务复核退出码 0（Target is up to date，1.28 秒）；9 个插件 C++ 文件 BOM/缩进/空白检查通过。
Lua 语法编译和全项目函数文档检查通过（564/564）。4 组 C++ 契约测试只编译，未执行。
编辑器当前在线、未发现未保存内容或地图包，但未获得必要重启确认，因此未加载新插件、检查窗口或生成首份 DataTable；旧 INI 继续作为过渡标签源。

编译成功不代表窗口已经显示，也不代表 .uasset 已生成；分别记录代码编译、编辑器加载、窗口、资产保存和标签注册结果。
只有实际保存且检查通过后，才能把旧 INI 迁移标为完成。打包及游戏运行验证不在本轮默认范围。

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-08-26 | 初版采用 Python 将声明式 Lua 表转换为 INI，完成 12 个标签及 Survival 字典查询 |
| 2026-08-26 | 用户明确要求通用 Gameplay C++ 插件和 UE 编辑器窗口；改为可扩展 SekiroGameplay 插件，原生 DataTable 为主要生成资产 |
| 2026-08-26 | 用户明确菜单形态：新增 Lua玩法 / LuaGameplay 顶层菜单，按注册功能提供下拉项，首项直接打开 GameplayTag 的 Lua 导入 |
