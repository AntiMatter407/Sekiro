---
paths:
  - "Docs/engine-reference/unreal/skills/**"
---

# UE 技能库规则

## 技能使用

在执行 UE 相关任务前，先检查 `Docs/engine-reference/unreal/skills/` 中是否有匹配的技能文件：

| 任务场景 | 对应技能目录 |
|----------|-------------|
| GAS / 技能系统 | `ue-gameplay-abilities` |
| 角色移动 | `ue-character-movement` |
| 动画 | `ue-animation-system` |
| 网络同步 | `ue-networking-replication` |
| UI / UMG | `ue-ui-umg-slate` |
| 输入系统 | `ue-input-system` |
| 物理/碰撞 | `ue-physics-collision` |
| AI / 导航 | `ue-ai-navigation` |
| Niagara 特效 | `ue-niagara-effects` |
| 材质/渲染 | `ue-materials-rendering` |
| Actor/Component 架构 | `ue-actor-component-architecture` |
| C++ 基础 | `ue-cpp-foundations` |
| 数据资产/DataTable | `ue-data-assets-tables` |
| 模块/构建系统 | `ue-module-build-system` |
| 音频 | `ue-audio-system` |
| 异步/线程 | `ue-async-threading` |
| 编辑器工具 | `ue-editor-tools` |
| GameFeatures | `ue-game-features` |
| 状态树 | `ue-state-trees` |
| Mass Entity | `ue-mass-entity` |
| PCG | `ue-procedural-generation` |
| Sequencer | `ue-sequencer-cinematics` |
| 存档/序列化 | `ue-serialization-savegames` |
| 测试/调试 | `ue-testing-debugging` |
| 关卡流送 | `ue-world-level-streaming` |
| 项目上下文 | `ue-project-context` |

## 技能维护

### 勘误
- 发现技能文件中 API 过时、示例错误或遗漏时，立即修正
- 修正后更新 frontmatter 中的 `version`（次版本号 +1）

### 扩充
- 项目中积累的 UE 最佳实践、常见陷阱、项目特有模式，应回写到对应技能文件
- 新增内容放在 `references/` 子目录中，保持 SKILL.md 精简

### 防止冗余（硬性约束）
- **SKILL.md 不超过 500 行**，超过则拆分到 `references/` 中
- **禁止重复**：同一知识点只在一处出现；其他技能通过引用链接
- **项目无关的通用内容不写入**：技能库聚焦项目实际需要的部分
- 每个技能只覆盖一个子系统，边界清晰不重叠

## 代理指令

所有 UE 相关代理（unreal-specialist、ue-gas-specialist 等）在给出代码建议前：
1. 读取 `Docs/engine-reference/unreal/VERSION.md` 确认引擎版本
2. 检查对应 `Docs/engine-reference/unreal/skills/ue-*/SKILL.md` 获取上下文
3. 如发现技能内容与实际不符，先更新技能再基于更新后的技能给出建议
