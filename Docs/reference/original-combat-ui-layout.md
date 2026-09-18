# 原版战斗 UI 资源与布局实施记录

## 已确认的来源

资源哈希、源成员、图集矩形见 [资产清单](original-combat-ui-assets.json)。原始安装目录只读；处理目录为工程 `Extracted/UI/OriginalCombat`。

| 文件/成员 | 核查结果 |
|---|---|
| `menu/01_000_fe.gfx` | 舞台 RECT 为 `(0,1920,0,1080)`，帧率 60，根时间轴 1 帧 |
| `menu/hi/01_common.sblytbnd.dcx` | BND4 解包后 `Menu/ScaleForm/SBLayout/01_Common/Hi/SB_FE.layout` 为 XML 图集坐标 |
| `menu/hi/01_common.tpf.dcx / SB_FE.dds` | 4096×1024、BC7_UNORM；包含玩家/Boss/普通目标生命及架势子图 |
| 同包 `MENU_Lockon01.dds` | 64×32、BC7_UNORM；RGBA 保留原透明像素，不做 AI 重绘或色彩替换 |

源 DDS 经现有 texconv 转为 8-bit RGBA PNG，导入插件直接解码像素创建 Texture2D。两张 UE 纹理使用 UI 组、sRGB、保留 alpha、NoMipmaps、NeverStream、Clamp。原图有 RGB=255 而 alpha=0 的像素，按 straight alpha 处理；不作预乘转换。

## GFX 显示树证据

位置以原版逻辑像素计。以下读取原文件的 DefineSprite、PlaceObject2/3、SymbolClass、DefineExternalImage2 和 DefineShape，不执行 ActionScript。首帧数据不能证明完整的运行时显隐、成长条长或时间轴切换。

| 显示树路径 | 原始数据/用途 |
|---|---|
| `PlayerHUD(1023)` | 根偏移 `(960,537)` |
| `PlayerHUD/Gauge(1022)` | 局部偏移 `(-885,400)` |
| `Gauge/HP(970)/Fade/Offset` | HP 偏移 `(125.85,45.65)`、Y 缩放 `0.879989624`，Offset 再偏移 `(-89,0)`；生命原点合成为 `(111.85,982.65)` |
| `HP/.../BarFrame/Bar(961)` | 1920 帧遮罩动画；第 600 帧右端标记位于 X=332.75。当前项目采用该帧宽度作为固定显示基线，未恢复原版生命成长到帧号的映射 |
| `Gauge/SP(1014)` | 局部偏移 `(885,0.5)`、XY 缩放约 1.3，合成中心 `(960,937.5)` |
| `Gauge/SPFrame/Fade/State_0/1/1` | `MENU_Taikan_base_03`，原版黑底及 alpha=0.6484375；多种临界状态的叠层尚未完整实现 |
| `EnemyTag(839)` | 同一目标下同时包含 HPGauge、SPFrame、SP；不能把普通敌架势一律放到固定顶部 |
| `EnemyTag/1` | `MENU_HP_bar_Enemy_base`，局部 `(-66.1,-4.4)`，0.5 倍，显示 133×11 |
| `EnemyTag/HPGauge/Current/3/1` | `MENU_HP_bar_Enemy`，合成局部 `(-63.75,-1.95)`，显示 128×6 |
| `BossList(870)/Item_0_0(869)` | 根 `(958.25,174.35)` 加 Item 偏移 `(98.7,-86.6)`；顶部固定区，由显式 Boss 展示入口选择对象 |
| `BossList/Item_0_0/HPGauge/Current/3/1` | `MENU_HP_bar_Boss`，合成约 `(112.45,90.9076)`，显示约 410.6475×17.8972 |
| `LockOn(1045)/1/1/1` | 引用 DefineShape 1042，矩形 `[-16,16]×[-16,16]`，位图填充引用 36=`MENU_Lockon01`；位图矩阵 20 twips/像素、平移 `(-16,-16)` |

锁定 Shape 因此仅取 64×32 纹理的**左侧 32×32**，UV 为 `(0,0)..(0.5,1)`，在 1920×1080 下显示 32×32。其有效 alpha 区域是中心白色亮点，不能将整张 64×32 拉成正方形，也不能沿用旧橙色刻线圆环。已读到源 LockOn 时间轴共 90 帧，但本版仅恢复静态标记，未把所有时间轴帧当成循环动画执行。

格式字段核验参考 JPEXS 项目自身的 [DefineExternalImage2](https://github.com/jindrapetrik/jpexs-decompiler/blob/master/libsrc/ffdec_lib/src/com/jpexs/decompiler/flash/tags/gfx/DefineExternalImage2.java) 与 [PlaceObject2](https://github.com/jindrapetrik/jpexs-decompiler/blob/master/libsrc/ffdec_lib/src/com/jpexs/decompiler/flash/tags/PlaceObject2Tag.java) 实现；上表游戏数值均来自本地原文件，不来自搜索结果或屏幕猜测。

## 当前实现边界

- 纹理和图集取样来自原文件；项目样式在 `Content/Script/Gameplay/Sekiro/UI/CombatHUDStyle.lua`，不保存生命上限、架势上限或恢复速度。
- 生命向右裁切；架势采用左右镜像半条向两端展开，源 UV 与目标显示区域同步裁切，不把整张原图压进剩余长度。
- 图集窗口、位置和源矩阵已核查；当前条形重建简化了原版的遮罩动画和色彩乘加。玩家红色是原白纹理 ColorTransform 白点的线性颜色近似，不是完整逐像素原色变换。
- 玩家血条当前固定采用源第 600 帧长度；GAS 的 MaxHealth 仍实时参与百分比，修改上限不会被 UI 拦截。但条长不会随上限按原版成长公式变化。
- 玩家、目标与 Boss 都只消费 Survival 提交后的快照；未初始化、非有限数值、非正上限不显示对应条，绝不默认满血。
- 普通目标暂由已有相机锁定指针驱动，不扫描全世界。非锁定受击目标展示仍依赖父任务的统一命中通知。
- Boss 通过 `ASKHUD::SetBossDisplayTarget(TargetActor, DisplayName)` 设置，传空清除，脱锁不清除；与普通锁定条去重。弦一郎蓝图与当前关卡实例显式设置 Actor Tag `UI.Display.Boss`，单 Boss 场景在 HUD 初始化时绑定一次；多 Boss 不任取第一项，新锁定 Boss 只在当前无展示对象时补充。尚无完整 Encounter 自动生产端，不渲染未经核查的字体、名称占位或忍杀节点。
- 锁定直接跟随角色共有的 `Pelvis` 骨骼，主角与弦一郎资产均已核对；不新增 Socket、不再使用包围盒高度补偿，骨骼不存在时隐藏。保留位置平滑但切目标时重置；按参考画布/视口尺寸适配，90 帧动画仍未校准。
- 固定 HUD 根据参考画布等比缩放并按锚点放置；目标投影仅一次 DPI 转换。专用 SafeZone 及多分辨率视觉对照尚未验收。
- 原生 Widget 直接由 UIManager 创建；本版没有生成空的 Widget Blueprint 或无用途的材质资产。需要原版复杂色变/动画时再新增相应资产。
- `/Game/UI/Combat` 显式纳入 Cook，避免 Lua 字符串引用的纹理遗漏；运行时不加载 Extracted、源 PNG、原版目录或编辑器模块。未执行打包验证。

## 重建与导入

1. 在项目配置中设置可读的 `SEKIRO_GAME_DIR`；工具路径由 `PipelineConfig` 提供。
2. 用 UE Python 执行 `Script/extract_combat_ui.py`。当前 `ext_tools/Yabber` 缺依赖，可显式传入工程已有完整发行包 `--yabber "Tools/Yabber 1.3.1/Yabber.exe"`；没有改动共享管线配置。
3. 编译 `SekiroEditor` 的 `Sekiro` 与 `SekiroGameplayEditor` 模块；经用户确认重新加载编辑器后进入 **Lua玩法 / LuaGameplay → 原版战斗 UI 资源导入**。
4. 选择 `Config/UI/OriginalCombatUI.json`，先预览，再导入。该 JSON 是编辑器素材清单，运行时布局仍为 Lua；离线 Python 不代替 C++ 导入窗口。
5. C++ 插件记录工具标识、源 PNG 相对路径、实际 PNG SHA1、源成员哈希和图集元数据；只更新本工具生成的同来源纹理，拒绝手工资产、脏包、只读文件和来源冲突。

静态布局/源文件检查、编译和成功导入不能替代游戏内视觉验收。本轮默认不启动 PIE，不执行玩家输入或运行场景。

## 实际导入结果

2026-08-26，用户确认继续后，在线编辑器已加载新增 UI 类，本次未重复重启。通过 C++ `ImportUITextureManifest` 成功生成并保存：

| UE 资产 | 尺寸 | 文件字节数 |
|---|---|---|
| `/Game/UI/Combat/Textures/T_OriginalCombatAtlas` | 4096×1024 | 4256556 |
| `/Game/UI/Combat/Textures/T_OriginalLockOn` | 64×32 | 4742 |

两张资产已在内容浏览器定位，UI组、sRGB、alpha保留、NoMipmaps、NeverStream、Clamp、PNG SHA1与来源元数据均只读核对通过。相对/绝对清单路径都能通过预检并识别同源更新，但没有为了测试而再次覆盖资产。
完整资产对象路径、设置、元数据和保存文件 SHA-256 见[实际导入记录](original-combat-ui-import.json)。本次没有修改 C++/Lua 运行时代码，没有启动 PIE、运行场景或打包；先前编译通过的代码和资产加载路径已对齐。
