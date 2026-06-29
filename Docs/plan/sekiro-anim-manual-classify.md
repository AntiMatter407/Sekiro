# 只狼动画系统 — 人工识别 + AI 状态机

| 状态 | 创建 | 更新 |
|------|------|------|
| 🔄 进行中 | 2026-06-27 | 2026-06-27 |

## 需求描述

采用 **人工识别动画内容 + AI 状态机制作** 的方式生成新的动画系统。

与原 `sekiro-anim-input-replica`（TAE 自动管线驱动）方案的区别：
- **原方案**：TAE 二进制 → JSON → C++ DataAsset → 运行时驱动
- **新方案**：人工分类整理已导入的动画资源 → AI 辅助设计状态机 → AnimBlueprint 搭建

核心思路：不依赖自动提取的准确性，而是由人判断每个动画的实际用途，再借助 AI 设计合理的状态机结构。

---

## ⏳ 任务

### 1. 标注动画内容

- ⏳ 1.1 **梳理已导入的动画资源清单**
  - 确认 `Content/Characters/Sekiro/Animations/` 下所有 AnimSequence
  - 整理 AnimID → 动画路径 → 原始 TAE 分类信息的对照表

- ⏳ 1.2 **人工识别每个动画的实际用途**
  - 按类别分组：Locomotion（待机/行走/跑步/冲刺/转向）、Attack（轻击/重击/连段/空中/垫步攻击）、Defense（格挡/弹刀/破防）、Dodge（闪避/垫步）、Jump（起跳/空中/落地）、Hit（小击退/大击退/击倒/死亡）、Other（忍杀/钩索/游泳/悬挂）
  - 标注方式：在 AnimSequence 资产上直接添加 Tag 或维护对照表 CSV

- ⏳ 1.3 **输出动画分类清单**
  - 格式：`AnimID, AssetPath, Category, SubCategory, Notes`
  - 作为 AI 状态机设计的输入

---

## 涉及文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `Content/Characters/Sekiro/Animations/` | 阅读 | 已导入的动画资源 |
| `Plugins/SekiroAssetManager/.../SATAELogicIR.h` | 参考 | AnimID 分类推断信息 |
| `Output/StateAnimMap.csv` | 参考 | c0000.hkx 提取的状态映射 |
| `Output/StateAnimMap_DataTable.json` | 参考 | DataTable 格式的状态映射 |

---

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-27 | 创建文档。替代搁置的 sekiro-anim-input-replica，采用人工识别+AI状态机新方案 |
