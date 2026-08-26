# Gameplay C++ 插件与 Lua GameplayTag 生成

| 状态 | 创建 | 更新 |
|------|------|------|
| 🔄 C++ 插件已实现并编译；编辑器生成迁移待确认 | 2026-08-26 | 2026-08-26 |

父需求：[Boss 与玩家对战基础](boss-player-combat-foundation.md) 2.6；关联 [GAS 数值基础](gas-numeric-foundation.md) 与 [Survival](survival-component.md)。
详细方案：[SekiroGameplay 插件](../design/lua-gameplay-tags.md)。

## 需求与范围

新增通用 **SekiroGameplay C++ 插件**，在 UE 主菜单栏增加中文 **Lua玩法** / 英文 **LuaGameplay** 顶层菜单。
按功能生成多个下拉项，首项 **GameplayTag 的 Lua 导入** 直接打开编辑器窗口，读取用户指定的层次 Lua 表，
预览并生成原生 GameplayTag DataTable，注册到 GAS 标签表列表。
插件需要可扩展功能注册入口，以后不仅用于 Tag；本轮不实现其他 Gameplay 业务。

本需求替代先前的 Python 命令行方案。Lua 表结构和既有 Survival Tag 名称保持不变，
但生成器、窗口、资产保存和标签注册都由 C++ 实现，不依赖 Python 或 UnLua。

## 任务树

- ✅ 1. 插件方案
  - ✅ 1.1 确定 SekiroGameplay 插件、编辑器模块与工具注册接口
  - ✅ 1.2 确定 UDataTable / FGameplayTagTableRow 产物及 GameplayTagTableList 注册
  - ✅ 1.3 保留 Lua 层次契约，明确来源所有权、写入保护与迁移顺序
- ✅ 2. C++ 实现
  - ✅ 2.1 纯 C++ Lua 数据解析、诊断与稳定标签预览
  - ✅ 2.2 公开 UFUNCTION 资产生成、受控更新、保存与注册
  - ✅ 2.3 可扩展停靠窗口、文件/路径选择、预览及生成反馈
  - ✅ 2.4 原生契约测试代码与 UBT 编译
  - ✅ 2.5 顶层 Lua玩法 / LuaGameplay 双语菜单、动态功能项与直接打开对应页面；编译验证通过
- 🔄 3. 项目迁移
  - ✅ 3.1 显式启用插件，移除 Python 生产工具及旧测试
  - ✅ 3.2 更新 Lua 注释、父子需求、插件使用文档
  - ⬜ 3.3 在用户授权的编辑器操作范围内生成并注册首份 DataTable
  - ⬜ 3.4 确认新表可用后移除旧生成 INI，保留其他标签源
- 🔄 4. 验证与交付
  - ✅ 4.1 复核模块编译、BOM、代码边界及静态检查
  - ⬜ 4.2 分别记录窗口/资产/标签源检查结果，不启动 PIE
  - ✅ 4.3 同步实际完成状态和使用入口

## 验收标准

- 插件可以通过 UBT 编译并由编辑器加载。
- 编辑器窗口支持从用户指定 Lua 文件预览和生成，错误可定位。
- 中文/英文顶层菜单分别显示 Lua玩法 / LuaGameplay；每个注册功能有独立下拉项，点击直接打开该功能，注销后菜单同步移除。
- 生成资产是 UE 原生 DataTable，使用 FGameplayTagTableRow，包含父节点与中文说明。
- 生成成功后可注册到 GameplayTagTableList，其他配置和资产保持不变。
- 目标资产来源不匹配、手写资产、非法表、非法路径、只读保存或配置失败不能被当作成功。
- 后续模块能注册新工具，不需要把新业务塞入 GameplayTag 解析器或窗口逻辑。
- 运行游戏不依赖编辑器插件；保留 Survival 标签缺失时的失败关闭行为。

## 验证记录

### 本轮 C++ 插件

- SekiroGameplayEditor 的 UBT 编译成功（exit 0），主任务再次编译退出码 0，Target is up to date（1.28 秒）。
- 编译日志：Script/temp/sekiro-gameplay-plugin-ubt-agent.log、Script/temp/sekiro-gameplay-plugin-ubt.log。
- Survival 的缺标签诊断改为兼容数据表/配置来源，Sekiro 模块复编通过（exit 0，23.59 秒）；日志 Script/temp/sekiro-gameplay-survival-ubt.log，仅有既有 UnLua 弃用警告。
- 9 个插件 .h/.cpp 的 UTF-8 BOM、4 空格缩进与行尾空白检查通过；模块没有 UnLua/Python/项目源码依赖。
- 4 组 C++ 契约测试（层级、恶形语法、资源上限、资产包路径）仅编译、未执行。
- 项目 Lua 标签表只改使用注释；Lua 语法编译通过，全项目函数文档 564/564，0 问题。
- .uproject/.uplugin JSON 及关联文档链接静态检查通过；旧 Python 生产工具和测试已移除。
- 编辑器在线，读取到无未保存内容包或地图包；尚未收到必要重启确认，未重启、未加载新插件、未验证窗口或创建首份 DataTable。
- 旧 Config/Tags/SKGameplayTags.ini 保留为过渡来源；仅在新表真实保存并确认注册后删除，不提前引用不存在的资产。
- 没有启动 PIE、模拟输入或执行游戏运行测试。

### 顶层菜单迭代验证

- 主菜单扩展点改为 LevelEditor.MainMenu，菜单名 LuaGameplay，显示文本为 Lua玩法 / LuaGameplay；按工具注册表生成独立下拉项。
- SekiroGameplayEditor UBT 退出码 0（5.86 秒）；日志 Script/temp/lua-gameplay-menu-ubt.log。
- 修改的两个 C++ 文件 BOM/空白检查、主菜单绑定及双语文本静态检查、文档链接检查通过；Lua 函数文档 564/564。
- 本轮未重启编辑器，未实际切换语言或验证菜单显示，未生成资产；首份 DataTable 与旧 INI 迁移继续保留待办。

### 早期 Python 记录

上一版 Python 生成了 12 个标签；一致性/幂等、Lua/Python 静态检查、Survival UBT 编译通过。
这些结果仅证明旧方案，不作为 C++ 插件窗口或 DataTable 的验证结果。
旧日志为 Script/temp/lua-gameplay-tags-ubt-agent.log 和 Script/temp/lua-gameplay-tags-ubt.log。

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-08-26 | 建立独立子需求并实现 Python → INI，首批 12 个标签与 Survival 查询接入完成 |
| 2026-08-26 | 用户修正产品形态：实现可扩展 Gameplay C++ 插件、UE 编辑器窗口和原生 GameplayTag DataTable；重新打开任务追踪迁移 |
| 2026-08-26 | 插件代码、菜单窗口、通用生成接口、扩展入口和4组测试编译完成；首份资产及旧INI迁移等待编辑器重启确认 |
| 2026-08-26 | 按用户要求将入口调整为 Lua玩法 / LuaGameplay 顶层菜单，注册功能自动成为下拉项，点击直接打开对应页 |
