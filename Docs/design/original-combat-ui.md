# 原版战斗 UI — 资源、排版与数据绑定设计

| 进度文档 | 状态 |
|----------|------|
| [原版战斗 UI 子需求](../plan/original-combat-ui.md) | 基础 UI 与导入插件已实现并编译；两张原版纹理已导入，完整视觉与运行验收待进行 |

## 1. 目标与使用结果

进入游戏后，本地玩家看到原版风格的生命与架势 HUD；敌方资源条根据普通目标/Boss 展示类型出现在对应区域。
锁定敌人时显示原版锁定素材，替换当前橙色圆环和刻线；切换目标只更换绑定对象，不创建第二套锁定标记。
伤害、治疗、架势增长/恢复、崩溃、死亡和回生都消费现有 GAS/Survival 结果，不增加 UI 侧数值权威。

本文保留完整设计目标；本次已实现的基础能力、编译结果和仍待完成内容见第 10 节及[资源与布局实施记录](../reference/original-combat-ui-layout.md)，不将规划项视为已验收。

## 2. 已有实现与可复用边界

| 已有位置 | 当前能力 | 本需求处理 |
|----------|----------|------------|
| Source/Sekiro/UI/SKHUD.h/.cpp | 本地 HUD 宿主、UIManager、Lua 初始化/Tick 桥接 | 复用宿主，不新增第二个 HUD |
| Content/Script/Gameplay/Sekiro/UI/SKHUD.lua | 初始化 UI 层、刷新玩家所有者 | 增加 HUD 生命周期编排与绑定 |
| Source/Sekiro/UI/SKUIManagerComponent.h/.cpp | 创建/管理 Widget、ZOrder、位置和显隐 | 复用管理能力，HUD 不抢输入焦点 |
| Source/Sekiro/UI/SKLockOnIndicatorComponent.h/.cpp | 当前锁定目标、屏幕投影、指示器类与显隐 | 保留目标/投影入口，更换资源显示 |
| Source/Sekiro/UI/SKLockOnIndicatorWidget.cpp | NativePaint 中程序绘制圆环、点和刻线 | 正式原版样式不再走旧绘制路径 |
| Content/Script/Gameplay/Sekiro/UI/SKLockOnIndicator.lua | 尺寸、橙色、距离缩放、位置平滑 | 改为读取原版样式，现有参数不当作原版事实 |
| USKSurvivalComponent | GetSurvivalSnapshot、OnSurvivalReady、OnHealthChanged、OnPostureChanged、OnLifeStateChanged | 作为本地角色/目标的提交后 UI 数据通知 |
| USKCharacterAttributeSet / ASC | 统一生命与战斗属性、初始化与属性变化通知 | 唯一权威，UI 只读 |

## 3. 原版资源证据

设计阶段通过本机路径检查及只读 GFX 字符串扫描定位如下候选；后续提取已确认 SB_FE 图集和 MENU_Lockon01 独立纹理，以下表格保留初始审计记录：

| 原版相对路径/资源名 | 当前证据 | 下一步 |
|---------------------|----------|--------|
| menu/01_000_fe.gfx | 文件存在，包含下列资源与布局符号 | 解析显示树、矩阵、遮罩和时间轴，不执行原版脚本 |
| menu/hi/01_common.sblytbnd.dcx | 高清布局候选包存在 | 解包并确认每个子资源与图集映射 |
| menu/hi/01_common.tpf.dcx | 高清纹理候选包存在 | 解包、核对纹理尺寸/通道/alpha |
| menu/low/01_common.sblytbnd.dcx、01_common.tpf.dcx | 低分辨率对应包存在 | 仅作对照或资源回退候选，不混用未经验证的 UV |
| MENU_HP_bar、MENU_HP_base、MENU_HP_25/50/75/100 | GFX 中发现符号 | 核验主角生命条各层，不按名称直接推定最终用途 |
| MENU_HP_Boss、MENU_HP_Boss_base、MENU_HP_bar_Boss | GFX 中发现符号 | 核验 Boss 血条纹理与布局 |
| MENU_Enemy_HP、MENU_Enemy_HP_Base、MENU_HP_bar_Enemy | GFX 中发现符号 | 核验普通敌人条形 UI |
| MENU_Taikan_bar、MENU_Taikan_base、MENU_Taikan_Center、MENU_Taikan_Edge、MENU_Taikan_Crash | GFX 中发现符号 | 核验架势填充、两端装饰和崩溃表现 |
| MENU_Lockon01、Lockon_Ani_297、LockOn | GFX 中发现符号 | 核验锁定图像、注册点和动画 |

扫描到名称不等于已经提取出图像；其中部分名称可能是显示对象、动画实例或别名。
本次已从显示树核查舞台、主要坐标、图集矩形和锁定裁切，证据转入[实施记录](../reference/original-combat-ui-layout.md)。原版完整脚本显隐、动态成长条长及动画时序仍未恢复。

## 4. 资源管线与编辑器入口

```text
用户指定的原版安装/已提取目录（只读）
  → 复制需要的原包到工程 Extracted/UI
  → 解包 GFX/布局/纹理并生成资源清单
  → 确认图集 UV、原始尺寸、alpha、边框、遮罩与动画
  → UE Texture2D / UI Material / Brush / Widget
  → 项目 Lua 样式和绑定配置
```

- 原版安装目录不写入、不就地解包；只导入本需求使用的资源，不复制整个游戏。
- 优先复用已有解包/纹理转换管线；格式不支持时记录缺口，不用自绘或生成图片冒充原素材。
- 为每项资源记录原包相对路径、哈希、原始符号、成员文件、尺寸、图集矩形/UV、旋转/裁切、UE 目标路径及生成器版本。
- 对彩色纹理和灰度遮罩分别确认 sRGB/压缩设置，使用 UI 纹理组；核对 straight/premultiplied alpha 后再选择材质混合，不盲目加颜色修正。
- 长条边框与填充分层；经证实可拉伸的中心才使用九宫格，装饰端头保持原比例。
- 生成物采用稳定名称与来源所有权，重复导入仅更新同源资产，禁止覆盖手写 Widget/材质。
- 在 **Lua玩法 / LuaGameplay → 原版战斗 UI 资源导入** 注册独立工具页，提供来源选择、候选预览、映射校验和生成结果。
- 原包/角色路径及样式映射由用户或项目 Lua/配置输入，SekiroGameplay 插件不得硬编码安装路径、素材 ID 或角色类。
- 运行时只加载已经导入的 UE 资源，不依赖本机原版安装目录、提取工具或编辑器模块。

## 5. 按原版排版的实施方式

先从 GFX 显示树、图集布局及同版本参考画面恢复原版基准坐标系，再映射到 UMG；纹理自身不能证明 HUD 坐标。
以下是需要分别核验的显示区域，不将初始区域划分等同于像素级复现完成：

| 展示对象 | 初始对齐目标 | 必须核对 |
|----------|--------------|----------|
| 玩家生命 | 屏幕左下资源区 | 上限变化时条长/分段、边距、背景与延迟层 |
| 玩家架势 | 屏幕下方中央 | 中心/两端填充规律、危险颜色、恢复和隐藏条件 |
| Boss 生命 | 屏幕上方左侧固定区 | 名称、边框、血条长度及关联装饰 |
| 敌方架势 | 原版目标架势区 | 普通敌人与 Boss 是否共用顶部区域、何时显示 |
| 普通敌人生命 | 原版目标跟随区 | 世界锚点、可见性、受击/锁定/距离规则 |
| 锁定标记 | 锁定目标的原版锚点 | 中心点、屏幕尺寸、旋转/脉冲、遮挡与失锁表现 |

具体偏移、填充方向、颜色阈值、淡入淡出和延迟条参数必须录入可追溯的样式配置后使用。
原版资料无法确认的表现单独标记“待核验”，不得悄悄换成通用 ProgressBar 默认布局。

### 分辨率与坐标

- 原版逻辑尺寸是配置字段，从原版布局读取；不直接假定源文件就是 1920×1080。
- 玩家/Boss 固定 HUD 使用明确 Anchor、Alignment、SafeZone 与 UMG DPI 曲线。
- 普通目标条和锁定点复用当前玩家视口投影，不将桌面像素、视口相对像素和 UMG 逻辑坐标混用。
- 当前锁定路径是 ProjectWorldLocationToScreen(..., true) → SetPositionInViewport(..., true)，迁移时保持单次 DPI 换算。
- 正常比例、1440p 和宽屏均需检查；宽屏保持装饰比例，不横向拉长整套原版素材。

## 6. GAS/Survival 数据绑定

### 6.1 资源含义

| 显示量 | 权威来源 | UI 规则 |
|--------|----------|---------|
| 生命当前值/上限 | Character AttributeSet 的 Health / MaxHealth | 最大值有效时 Health/MaxHealth，夹取到 0..1 |
| 架势当前值/上限 | Character AttributeSet 的 Posture / MaxPosture | Posture/MaxPosture，值越高越接近崩溃；不当作剩余耐力反转 |
| 生命轮次/死亡回生状态 | Survival Snapshot | 处理重置、可见性及动画状态，不推算回生次数 |
| 架势崩溃 | Survival 的 bPostureBroken / 相应事件 | 崩溃反馈以状态边沿为准，不因数值清零擅自解除 |
| 名称/普通或 Boss 展示样式 | 项目角色 UI 显示资料 | 只定义显示身份，不在 UI 内用类名硬编码选择 |
| Boss 忍杀节点、阶段 | 后续 Encounter/Phase 系统 | 未接入时明确未实现，不能用 MaxHealth 或回生字段伪造 |

UI 样式可保存当前显示百分比、补间目标和时间，不保存另一份业务血量/架势/恢复速度。

### 6.2 生命周期

1. 本地 HUD 创建时解析当前 Pawn、ASC、Survival；未就绪时隐藏对应资源区，不能默认显示满血。
2. 绑定就绪、资源变化及生命状态通知后读取一次完整快照，覆盖“初始化事件早于 HUD”的情况。
3. 使用 Survival 提交后的完整快照刷新当前值与上限；不要同时订阅多个来源重复播放同次受击反馈。
4. Tick 只用于必要的显示补间和世界点投影，不逐帧扫描角色或计算伤害/恢复。
5. 换 Pawn、切目标、死亡、目标销毁、UI EndPlay 时解绑旧委托，取消旧对象的异步资源回调和补间。
6. 以绑定代次/弱对象引用过滤旧事件；异步载入完成时再次校验角色和 LifeSerial，避免上一目标数据覆盖新目标。
7. 快照无效、数值非有限或最大值非正时隐藏无效条并诊断，禁止除零或伪造默认值。

## 7. 敌方目标来源

敌方 UI 的显示对象不应一律等同于相机锁定目标。

- 锁定标记只消费 CameraManager 当前锁定目标，UI 不自行选敌人。
- 普通敌人条遵循原版受击/锁定/距离可见性规则，目标集合来自已有命中/目标通知或通用展示入口，不每帧遍历全世界。
- Boss 固定 UI 消费 Encounter 指定的展示对象，脱锁不应自动误切为别的敌人；Encounter 未实现前，调试场景显式配置目标，不能声称完成 Boss 正式生命周期。
- 当前调试场景在弦一郎蓝图及放置实例的 Actor Tags 中显式配置 `UI.Display.Boss`；标签名集中在 `CombatHUDStyle.BossActorTag`。它只表示 UI 身份，不加入 GAS AttributeSet，也不承担 GameplayTag 的战斗状态职责。HUD 初始化仅查询一次：唯一标记对象自动提交给 `SetBossDisplayTarget`；零个等待后续指定，多个不按遍历顺序选择。新锁定标记对象仅在没有已有 Boss 时补充绑定，脱锁和切换普通目标不清除 Boss。完整入场/退战与多 Boss 生命周期仍交由后续 Encounter。
- 同一敌人同时具有目标条与 Boss 样式时，由显示策略去重；目标销毁后立即解绑并隐藏。
- 完整的未锁定受击显示依赖父任务统一命中协议，Boss 名称/阶段/节点依赖对应生产端；资源提取及玩家 HUD 可提前实施。

## 8. 锁定标记替换

保留 SKLockOnIndicatorComponent 的 WidgetClass 注入及目标/投影查询，保留 CameraManager 的搜索、评分、切换与失锁规则。
将 SKLockOnIndicatorWidget 的正式绘制路径改为可配置原版 Brush/Material 或 UMG Image，不能在其上叠加旧 NativePaint 圆环。

- 新样式需明确声明使用原版素材；旧程序图形最多作为显式调试选项，默认关闭。
- 原版素材缺失时提示资源问题，不静默把旧图形当成“原版替换已完成”。
- 当前 Lua 的 46 像素、橙色、NearScale/FarScale 等是旧效果参数，不直接带入原版样式。
- 目标锚点优先使用角色可配置锁定 Socket/偏移；没有配置时明确采用既有包围盒回退，后续按原版证据校准。
- 处理投影失败、摄像机背后、无效目标、失锁、暂停与分辨率变化；是否显示离屏标记只按原版证据决定，不新增未经要求的箭头。

## 9. 文件与职责规划

| 位置 | 职责 |
|------|------|
| Plugins/SekiroGameplay/Source/SekiroGameplayEditor/ 下独立 UI 导入功能 | 通用菜单页、导入/映射接口、预览和受控更新；由 plugin-programmer 实现 |
| Source/Sekiro/UI/SKCombatHUDWidget.h/.cpp（规划） | 通用显示数据/资源接口，不选择具体素材或编排战斗规则；由 gameplay-programmer 实现 |
| Source/Sekiro/UI/SKLockOnIndicatorWidget.h/.cpp | 保留宿主，更换为资源化显示入口并关闭旧绘制 |
| Content/Script/Gameplay/Sekiro/UI/SKHUD.lua 与新增 HUD Lua | 生命周期绑定、目标显示策略和 UI 事件编排 |
| Content/Script/Gameplay/Sekiro/UI/CombatHUDStyle.lua（规划） | 项目原版资源映射、布局和演出参数；不包含业务血量上限 |
| Content/UI/Combat/Textures、Materials、Widgets（规划） | 导入原版素材及生成 UI 资产 |
| Extracted/UI、工程内资源清单（规划） | 可复现提取中间结果、来源哈希与图集/布局映射 |

只有在证明现有接口不足后才扩展 C++。项目运行时行为不能放入编辑器插件；编辑器导入页也不依赖项目角色类。

## 10. 验证与推进门槛

资源阶段先核对来源、尺寸、alpha、UV、边框和原版布局证据，再进入正式 Widget 生成。
编译与必要资产生成不代表游戏内视觉验收；默认只做对应 UBT、Lua 静态和必要 Widget/Material 生成编译。

用户授权运行场景后再验收：

- 玩家与敌方生命的满值、部分、零值、治疗、上限变化和回生。
- 架势增长、恢复、临界与崩溃；同次致死/崩溃事件不会显示矛盾状态。
- 有无锁定、连续切目标、目标被销毁、Boss 脱锁、镜头快速移动与目标出屏。
- 多种分辨率下对照同版本原版画面，记录素材/布局/动画的已知差异。

### 10.1 本次实施结果

- 已只读复制并解包原包，使用 texconv 生成 4096×1024 图集 PNG 和 64×32 锁定 PNG；`Config/UI/OriginalCombatUI.json` 提供实际导入清单，来源 SHA-256/图集矩形独立存档。
- `USKCombatHUDWidget` 提供原图/UV、裁切方向、镜像、参考画布及单次 DPI 投影接口；UIManager 直接创建原生 Widget，不生成无用途的空 Widget Blueprint。
- `SKCombatHUD.lua` 绑定玩家、普通锁定目标、独立 Boss 的 Survival 快照和生命周期；普通目标生命/架势均跟随目标，Boss 固定架势单独分组。
- 每个槽位和每种通知使用稳定独立 Lua 回调，避开本项目 UnLua 按 `(SelfObject,LuaFunction)` 缓存委托处理器导致的签名复用问题；离场/换 Pawn/换目标均显式解绑。
- `SKLockOnIndicator` 使用原白色贴图的左侧 32×32，旧圆环默认关闭；保留目标算法、投影和平滑，移除旧距离缩放。
- 用户确认改用现有骨骼后，`CombatHUDStyle.LockOn.TargetBoneName = "Pelvis"` 驱动原生投影。已只读核对主角 `Sekiro_Model` 与弦一郎 `Genichiro_Model` 均存在该骨骼（索引均为 9，但运行时只按名称查找）。原生从 Character 主网格读取动画骨骼世界坐标，移除包围盒中心及 `+24cm` 高度补偿；缺骨骼或投影失败隐藏，不退回 Actor 原点，不新增 Socket。切换目标时重置屏幕平滑，避免从上一目标位置滑入。
- 弦一郎蓝图与 ThirdPersonMap 中对应的外部 Actor 资产均已保存 Boss 标记；该对象生命条固定左上、架势条固定顶部中央，普通锁定目标条去重。这里不新增 Boss 名称字体、阶段或忍杀节点。
- 骨骼接口参数已由高度数值改为 `FName`，Development UBT/UHT 已通过并生成对应反射参数。当前在线编辑器仍加载旧 DebugGame 模块；该配置的 UBT 因 Live Coding 活跃而停止，需用户授权关闭编辑器后补编译并重开。尚未加载新接口或运行 PIE，视觉结果不作已验收承诺。
- SekiroGameplay 插件通过 RegisterTool 加入 **原版战斗 UI 资源导入** 窗口；C++ 完成 JSON/PNG 校验、同源保护、像素解码、Texture2D 设置与保存，不调用 Python 导入或 AssetImportTask。
- 两模块 UBT 已通过，Lua/Python 语法和 C++ BOM 静态检查通过。新增导入边界测试只编译，未执行；没有 PIE、玩家输入或运行场景测试。
- 用户确认继续后检测在线编辑器已加载新增类，无需再次重启；通过 C++ 接口实际生成并保存 `T_OriginalCombatAtlas` 和 `T_OriginalLockOn`，尺寸、UI设置、来源哈希及磁盘文件均已核对，见[实际导入记录](../reference/original-combat-ui-import.json)。资产导入完成，不等于原版完整 UI 已验收。

### 10.2 明确保留的后续工作

原版完整延迟血条、逐像素色彩乘加、临界动画、90 帧锁定时序、生命成长到条长映射，以及 SafeZone/多分辨率视觉对照尚未完成。
首版玩家条长取源第 600 帧作为项目显示基线，原纹理不做重绘；Boss 名称字体、忍杀节点和 Encounter 自动生产端仍未接入，非锁定受击展示等待统一命中通知。

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-08-26 | 修正 Lua 初始化反射入口；接入显式 Boss 展示标记与 Pelvis 骨骼锁定定位，不新增 Socket；Development 编译与静态检查通过，编辑器重载及视觉验收另行确认 |
| 2026-08-26 | 建立原版 UI 复现方案；定位本机 GFX/布局/纹理包及候选资源名，明确 GAS 只读绑定、普通目标/Boss 显示来源和锁定替换边界 |
| 2026-08-26 | 提取真实图集与锁定 PNG，完成基础原图 HUD/Lua 绑定、锁定资源化与 C++ 导入窗口；编译/静态检查通过，实际导入等待编辑器重载确认 |
| 2026-08-26 | 两张原版 Texture2D 已经 C++ 导入器生成并保存；核对尺寸/设置/来源和同源识别，未运行 PIE |
