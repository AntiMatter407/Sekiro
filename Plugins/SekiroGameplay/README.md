# SekiroGameplay

通用 Gameplay C++ 编辑器插件，在 UE 主菜单栏增加 **Lua玩法 / LuaGameplay** 顶层菜单。首个功能是从声明式 Lua 表生成原生 GameplayTag DataTable；后续工具通过注册接口增加下拉项。

## 使用

1. 编译项目并启用 **SekiroGameplay**，新安装插件后重启编辑器。
2. 从主菜单栏 **Lua玩法 → GameplayTag 的 Lua 导入** 直接打开功能窗口；英文界面为 **LuaGameplay → Import GameplayTags from Lua**。
3. 选择 Lua 文件，填写输出资产长包名，例如 `/Game/Gameplay/AbilitySystem/DT_GameplayTags`。
4. 点击预览查看展开的标签与说明，再点击生成。
5. 默认注册生成的 DataTable 到项目 GameplayTagTableList，并刷新标签树；生成后可定位资产。

窗口必须在非 PIE 状态使用。生成工具不依赖 Python、UnLua 或外部 Lua 运行时。
顶层菜单与“文件、编辑、工具”等菜单同级，放在“帮助”之前，不再挂到“工具”菜单下面。
菜单与内置下拉项通过 UE 本地化文本随编辑器语言切换；其他语言暂使用英文回退。

## Lua 输入

```lua
return {
    State = {
        _Comment = "角色状态",
        Life = {
            Dead = "死亡完成",
        },
    },
}
```

每级节点生成一个标签，上例生成 `State`、`State.Life`、`State.Life.Dead`。
字符串是说明，`true` 或空表表示无说明标签，`_Comment` 是当前节点说明。
只接受独立声明式表，不执行 Lua 代码。重复键、大小写冲突、非法节点和超出资源上限都会拒绝。

## 生成物与更新保护

- 产物是引擎 `UDataTable`，行结构为 `FGameplayTagTableRow`，不是自定义运行时资产。
- 已有表只有在生成器所有权、Lua 来源及行结构均匹配时才允许更新；手写资产不会被覆盖。
- 资产保存与配置注册分开报告，配置失败不能被当作全部成功。
- 不删除其他标签来源，不自动修复蓝图/GE/存档里的旧标签引用。
- 生成的标签字典不会自动向角色授予状态；角色仍由 GAS/GameplayEffect 管理标签贡献。

## C++ 扩展

模块 `SekiroGameplayEditor` 的公共接口 `FSekiroGameplayEditorModule::RegisterTool` 接收工具 ID、本地化的显示名称及 Slate Widget 工厂。
每个注册功能会成为顶层菜单中的独立下拉项，点击后直接打开对应页面；下次展开时自动读取最新注册列表。
卸载扩展模块时调用 `UnregisterTool`，对应菜单项和页面一起移除；无需修改 GameplayTag 解析器。
`OpenTool(ToolId)` 可直接打开某个功能，重复打开当前页只聚焦，不清空已填写的输入。

首个工具使用 `USekiroGameplayTagLibrary::PreviewGameplayTags` 和 `GenerateGameplayTagTable`，可供编辑器蓝图或自动化调用。
插件只包含编辑器模块，生成表不依赖该模块；后续有真实运行时职责时再增加运行时模块。

详细数据契约、迁移顺序和验证记录见项目 `Docs/design/lua-gameplay-tags.md` 与 `Docs/plan/lua-gameplay-tags.md`。

## 当前验证状态

插件已通过 UBT 编译；4 组 C++ 契约测试仅编译未运行。尚未重启当前编辑器验证窗口或生成项目首份 DataTable。
项目旧标签 INI 暂时保留，待新数据表保存并注册后再迁移，避免中途丢失 Survival 必需标签。
